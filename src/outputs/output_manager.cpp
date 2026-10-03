// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "outputs/output_manager.h"

#include "app/app_context.h"
#include "utils/i18n.h"
#include "utils/log.h"

#include <obs.h>

#include <algorithm>
#include <format>

namespace rd {

namespace {

constexpr const char *kOutputId = "rtmp_output";
constexpr const char *kServiceId = "rtmp_custom";

// Passed to OBS as the callback parameter of one output's signals. It lives as long as the
// signals are connected. Disconnecting waits for a running callback to return, so nothing
// reads a context after its session released it.
struct SignalContext {
	OutputManager *manager = nullptr;
	std::string id;
	uint64_t generation = 0;
};

} // namespace

struct OutputManager::Session {
	explicit Session(const IClock &clock) : state(clock) {}

	std::string id;
	DestinationStateMachine state;
	uint64_t generation = 0;

	// What this run uses. Kept after a stop so the card can still show the last settings.
	EffectiveDestination effective;
	std::string providerId;
	std::string name;
	bool holdsVideo = false;

	OBSOutputAutoRelease output;
	OBSServiceAutoRelease service;
	OBSEncoderAutoRelease videoEncoder;
	OBSEncoderAutoRelease audioEncoder;
	std::unique_ptr<SignalContext> signalContext;
	OBSSignal startSignal;
	OBSSignal stopSignal;
	OBSSignal reconnectSignal;
	OBSSignal reconnectSuccessSignal;

	DestinationStats stats;
	uint64_t lastBytes = 0;
	int lastTotalFrames = 0;
	int lastDroppedFrames = 0;
	int64_t lastSampleMs = 0;
};

OutputManager::OutputManager(AppContext &app, QObject *parent) : QObject(parent), app_(app)
{
	sampleTimer_.setInterval(1000);
	connect(&sampleTimer_, &QTimer::timeout, this, &OutputManager::sample);
	sampleTimer_.start();
}

OutputManager::~OutputManager()
{
	shutdown();
}

// ---- Sessions ----------------------------------------------------------------------------------

OutputManager::Session &OutputManager::session(const std::string &id)
{
	auto it = sessions_.find(id);
	if (it == sessions_.end()) {
		auto created = std::make_unique<Session>(app_.clock());
		created->id = id;
		it = sessions_.emplace(id, std::move(created)).first;
	}
	return *it->second;
}

OutputManager::Session *OutputManager::findSession(const std::string &id)
{
	const auto it = sessions_.find(id);
	return it == sessions_.end() ? nullptr : it->second.get();
}

const OutputManager::Session *OutputManager::findSession(const std::string &id) const
{
	const auto it = sessions_.find(id);
	return it == sessions_.end() ? nullptr : it->second.get();
}

void OutputManager::releaseObsObjects(Session &s)
{
	// Disconnect first. After this returns no signal handler runs for this output.
	s.startSignal.Disconnect();
	s.stopSignal.Disconnect();
	s.reconnectSignal.Disconnect();
	s.reconnectSuccessSignal.Disconnect();
	s.signalContext.reset();

	// Order matters: the output lets go of its encoders, then the encoders let go of the
	// video source, then the video source can be released.
	s.output = nullptr;
	s.service = nullptr;
	s.videoEncoder = nullptr;
	s.audioEncoder = nullptr;

	if (s.holdsVideo) {
		app_.releaseVideo(s.effective.video);
		s.holdsVideo = false;
	}
}

// ---- Start -------------------------------------------------------------------------------------

bool OutputManager::failStart(Session &s, UserMessage message)
{
	releaseObsObjects(s);
	logWarning("{} did not start. {}", s.name.empty() ? s.id : s.name, message.text());
	s.state.startFailed(std::move(message));
	Q_EMIT destinationChanged(QString::fromStdString(s.id));
	return true;
}

bool OutputManager::start(const std::string &id, bool privateTest)
{
	if (shutDown_)
		return false;

	const DestinationConfig *config = app_.config().findDestination(id);
	if (!config)
		return false;

	Session &s = session(id);
	if (!s.state.requestStart(privateTest))
		return false;

	s.generation = nextGeneration_++;
	s.name = config->name;
	s.providerId = config->provider;
	s.stats = {};
	s.lastBytes = 0;
	s.lastTotalFrames = 0;
	s.lastDroppedFrames = 0;
	s.lastSampleMs = 0;
	Q_EMIT destinationChanged(QString::fromStdString(id));

	const IProvider *provider = app_.providers().find(config->provider);
	if (!provider) {
		return failStart(s, {locf("Start.UnknownProvider", "{0} uses a platform this RelayDock version does not know.",
					 config->name),
				     locf("Start.UnknownProvider.Detail", "The platform id is \"{0}\".", config->provider),
				     loc("Start.UnknownProvider.Action",
					"Update RelayDock, or remove this destination and add it again.")});
	}

	// Configuration problems. Nothing has touched OBS yet.
	const std::vector<ValidationIssue> issues = app_.validateDestination(id);
	for (const ValidationIssue &issue : issues) {
		if (issue.severity == Severity::Error)
			return failStart(s, issue.message);
	}

	if (privateTest && provider->info().testSupport != TestSupport::PrivateStream) {
		return failStart(s, {locf("Start.NoPrivateTest", "{0} has no private test mode.", provider->info().displayName),
				     {},
				     loc("Start.NoPrivateTest.Action",
					"Use Test connection to check that the server is reachable.")});
	}

	// Effective settings, computed over every enabled destination so that a destination
	// started now matches the ones that are already live and can share their encoder.
	const std::vector<EffectiveDestination> all = app_.resolveEffective(id);
	const auto found = std::find_if(all.begin(), all.end(),
					[&](const EffectiveDestination &destination) { return destination.id == id; });
	if (found == all.end() || !found->usable()) {
		return failStart(s, {locf("Start.NoEncoder", "{0} has no video encoder to use.", config->name),
				     loc("Start.NoEncoder.Detail", "OBS reports no H.264 encoder that the RTMP output accepts."),
				     loc("Start.NoEncoder.Action",
					"Check that OBS itself can stream with the x264 encoder, then restart OBS.")});
	}
	s.effective = *found;

	const StreamContext context = app_.streamContext();
	const VideoEncoderCaps *caps = context.findVideoEncoder(s.effective.video.encoderId);
	if (!caps) {
		return failStart(s, {locf("Start.EncoderGone", "{0} cannot use the encoder \"{1}\".", config->name,
					 s.effective.video.encoderId),
				     {},
				     loc("Start.EncoderGone.Action", "Pick another encoder in the destination's Video settings.")});
	}

	// Video source
	std::string error;
	video_t *frames = app_.acquireVideo(s.effective.video, error);
	if (!frames) {
		return failStart(s, {locf("Start.NoVideo", "{0} has no video to send.", config->name), error,
				     s.effective.video.orientation == Orientation::Vertical
					     ? loc("Start.NoVideo.ActionVertical", "Open the vertical layout editor and check the layout.")
					     : loc("Start.NoVideo.Action", "Check the OBS video settings.")});
	}
	s.holdsVideo = true;

	// Encoders
	const obs_scale_type scaleType = app_.config().performanceMode == PerformanceMode::Potato ? OBS_SCALE_BILINEAR
												     : OBS_SCALE_BICUBIC;
	s.videoEncoder = pool_.acquireVideo(s.effective.video, *caps, frames, scaleType, error);
	if (!s.videoEncoder) {
		return failStart(s, {locf("Start.VideoEncoderFailed", "{0} could not create its video encoder.", config->name),
				     error,
				     loc("Start.VideoEncoderFailed.Action",
					"Pick another encoder in the destination's Video settings, or update your graphics driver.")});
	}
	s.audioEncoder = pool_.acquireAudio(s.effective.video, s.effective.audio, error);
	if (!s.audioEncoder) {
		return failStart(s, {locf("Start.AudioEncoderFailed", "{0} could not create its audio encoder.", config->name),
				     error, loc("Start.AudioEncoderFailed.Action", "Pick another audio encoder in the destination's Audio settings.")});
	}

	// Service: where to connect and what to authenticate with. The key goes from the
	// credential store straight into OBS memory. It is never written to RelayDock's settings.
	const Endpoint endpoint = provider->endpoint(*config);
	{
		OBSDataAutoRelease serviceSettings = obs_data_create();
		obs_data_set_string(serviceSettings, "server", endpoint.serverUrl.c_str());

		SecretString key;
		app_.vault().get({id, CredentialKind::StreamKey}, key);
		const SecretString publishKey = provider->publishKey(key, privateTest);
		obs_data_set_string(serviceSettings, "key", publishKey.reveal().c_str());

		if (config->useAuth) {
			SecretString password;
			app_.vault().get({id, CredentialKind::Password}, password);
			obs_data_set_bool(serviceSettings, "use_auth", true);
			obs_data_set_string(serviceSettings, "username", config->username.c_str());
			obs_data_set_string(serviceSettings, "password", password.reveal().c_str());
		}

		const std::string serviceName = std::format("RelayDock service: {}", config->name);
		s.service = obs_service_create_private(kServiceId, serviceName.c_str(), serviceSettings);
	}
	if (!s.service) {
		return failStart(s, {locf("Start.NoService", "{0} could not set up its connection.", config->name),
				     loc("Start.NoService.Detail", "OBS did not create the RTMP service. The rtmp-services module may be missing."),
				     loc("Start.NoService.Action", "Repair or reinstall OBS Studio.")});
	}

	// Output
	{
		OBSDataAutoRelease outputSettings = obs_data_create();
		if (!config->connection.bindIp.empty())
			obs_data_set_string(outputSettings, "bind_ip", config->connection.bindIp.c_str());

		const std::string outputName = std::format("RelayDock: {}", config->name);
		s.output = obs_output_create(kOutputId, outputName.c_str(), outputSettings, nullptr);
	}
	if (!s.output) {
		return failStart(s, {locf("Start.NoOutput", "{0} could not create its stream output.", config->name),
				     loc("Start.NoOutput.Detail", "OBS did not create the RTMP output. The obs-outputs module may be missing."),
				     loc("Start.NoOutput.Action", "Repair or reinstall OBS Studio.")});
	}

	obs_output_set_service(s.output, s.service);
	obs_output_set_video_encoder(s.output, s.videoEncoder);
	obs_output_set_audio_encoder(s.output, s.audioEncoder, 0);

	// OBS retries with a growing wait (1.5x each time, capped at 15 minutes), so a dead
	// server never causes a tight reconnect loop.
	if (config->connection.autoReconnect)
		obs_output_set_reconnect_settings(s.output, config->connection.reconnectAttempts,
						  config->connection.reconnectDelaySec);
	else
		obs_output_set_reconnect_settings(s.output, 0, 0);

	obs_output_set_delay(s.output, static_cast<uint32_t>(config->connection.streamDelaySec),
			     OBS_OUTPUT_DELAY_PRESERVE);

	s.signalContext = std::make_unique<SignalContext>();
	s.signalContext->manager = this;
	s.signalContext->id = id;
	s.signalContext->generation = s.generation;

	signal_handler_t *handler = obs_output_get_signal_handler(s.output);
	s.startSignal.Connect(handler, "start", onOutputStart, s.signalContext.get());
	s.stopSignal.Connect(handler, "stop", onOutputStop, s.signalContext.get());
	s.reconnectSignal.Connect(handler, "reconnect", onOutputReconnect, s.signalContext.get());
	s.reconnectSuccessSignal.Connect(handler, "reconnect_success", onOutputReconnectSuccess, s.signalContext.get());

	logInfo("Starting {} ({}) to {}{}", config->name, provider->info().displayName, endpoint.serverUrl,
		privateTest ? " as a private test" : "");

	if (!obs_output_start(s.output)) {
		const char *lastError = obs_output_get_last_error(s.output);
		const std::string obsError = lastError ? lastError : "";
		UserMessage message = provider->describeStop(config->name, StopReason::Error, obsError);
		message.what = locf("Start.Refused", "{0} could not start.", config->name);
		return failStart(s, std::move(message));
	}

	return true;
}

int OutputManager::startAllEnabled()
{
	int started = 0;
	// Copy the ids first. Starting emits signals and the list could change under the loop.
	std::vector<std::string> ids;
	for (const DestinationConfig &destination : app_.config().destinations) {
		if (destination.enabled)
			ids.push_back(destination.id);
	}
	for (const std::string &id : ids) {
		const Session *existing = findSession(id);
		if (existing && !existing->state.canStart())
			continue;
		if (start(id))
			++started;
	}
	return started;
}

// ---- Stop --------------------------------------------------------------------------------------

void OutputManager::joinFinishedStopThreads(bool waitForAll)
{
	for (auto it = stopThreads_.begin(); it != stopThreads_.end();) {
		if (waitForAll || it->done->load()) {
			if (it->thread.joinable())
				it->thread.join();
			it = stopThreads_.erase(it);
		} else {
			++it;
		}
	}
}

void OutputManager::joinStopThreadsFor(const std::string &id)
{
	for (auto it = stopThreads_.begin(); it != stopThreads_.end();) {
		if (it->id == id) {
			if (it->thread.joinable())
				it->thread.join();
			it = stopThreads_.erase(it);
		} else {
			++it;
		}
	}
}

void OutputManager::stopOutputAsync(Session &s, bool force)
{
	if (!s.output)
		return;

	joinFinishedStopThreads(false);

	// OBS can block inside a stop while a connection attempt is still in flight, for as long
	// as the operating system takes to give up on the connection. Run the stop on a worker
	// so the OBS window stays responsive. The worker holds its own reference to the output.
	OBSOutput output = s.output.Get();
	auto done = std::make_shared<std::atomic<bool>>(false);

	StopThread entry;
	entry.id = s.id;
	entry.done = done;
	entry.thread = std::thread([output, force, done]() {
		if (force)
			obs_output_force_stop(output);
		else
			obs_output_stop(output);
		done->store(true);
	});
	stopThreads_.push_back(std::move(entry));
}

void OutputManager::stop(const std::string &id)
{
	Session *s = findSession(id);
	if (!s)
		return;

	const DestinationPhase before = s->state.runtime().phase;
	if (before == DestinationPhase::Stopping) {
		// Second press: stop now, without waiting for delayed or unsent data. A restart that
		// a manual reconnect had queued is dropped, because the user asked for a stop.
		s->state.cancelRestart();
		logInfo("Forcing {} to stop now.", s->name);
		stopOutputAsync(*s, true);
		return;
	}

	if (!s->state.requestStop())
		return;

	logInfo("Stopping {}.", s->name);
	// A graceful stop does nothing while OBS is still connecting or waiting to reconnect.
	// Those cases need the forced stop.
	const bool live = before == DestinationPhase::Live;
	stopOutputAsync(*s, !live);
	Q_EMIT destinationChanged(QString::fromStdString(id));
}

void OutputManager::reconnect(const std::string &id)
{
	Session *s = findSession(id);
	if (!s)
		return;

	const DestinationPhase before = s->state.runtime().phase;
	if (!s->state.requestReconnect())
		return;

	logInfo("Reconnecting {} on request.", s->name);
	// Cut the connection now. The stop handler starts the destination again.
	stopOutputAsync(*s, true);
	(void)before;
	Q_EMIT destinationChanged(QString::fromStdString(id));
}

OutputManager::ApplyResult OutputManager::applyEffectiveChanges(const std::vector<std::string> &ids, bool allowReconnect)
{
	ApplyResult result;
	if (shutDown_)
		return result;

	const StreamContext context = app_.streamContext();
	auto inChange = [&](const std::string &id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };

	// What each destination should stream with now.
	std::map<std::string, EffectiveDestination> wanted;
	for (const std::string &id : ids) {
		const std::vector<EffectiveDestination> all = app_.resolveEffective(id);
		const auto found = std::find_if(all.begin(), all.end(),
						[&](const EffectiveDestination &destination) { return destination.id == id; });
		if (found != all.end() && found->usable())
			wanted[id] = *found;
	}

	std::vector<std::string> restart;
	// Running encoders that can take the new bitrate in place, with the sessions they serve.
	std::map<obs_encoder_t *, std::vector<Session *>> retunable;

	for (const std::string &id : ids) {
		Session *s = findSession(id);
		const auto target = wanted.find(id);
		if (!s || !s->output || target == wanted.end())
			continue;
		const DestinationPhase phase = s->state.runtime().phase;
		if (phase != DestinationPhase::Live && phase != DestinationPhase::Reconnecting)
			continue;
		if (target->second.video == s->effective.video && target->second.audio == s->effective.audio)
			continue; // Already streaming with these settings

		EffectiveVideo bitrateOnly = s->effective.video;
		bitrateOnly.bitrateKbps = target->second.video.bitrateKbps;
		const VideoEncoderCaps *caps = context.findVideoEncoder(s->effective.video.encoderId);
		const bool live = phase == DestinationPhase::Live && s->videoEncoder;
		if (live && caps && caps->dynamicBitrate && bitrateOnly == target->second.video &&
		    target->second.audio == s->effective.audio)
			retunable[s->videoEncoder.Get()].push_back(s);
		else
			restart.push_back(id);
	}

	for (auto &entry : retunable) {
		std::vector<Session *> &members = entry.second;
		const EffectiveVideo from = members.front()->effective.video;
		const EffectiveVideo &to = wanted[members.front()->id].video;

		// The encoder changes for everyone attached to it. That is only right when every
		// destination on it is part of this change and wants the same new settings.
		bool everyone = true;
		for (const auto &other : sessions_) {
			if (other.second->videoEncoder.Get() != entry.first)
				continue;
			const auto target = wanted.find(other.first);
			if (!inChange(other.first) || target == wanted.end() || !(target->second.video == to))
				everyone = false;
		}

		const VideoEncoderCaps *caps = context.findVideoEncoder(from.encoderId);
		if (everyone && caps && pool_.retune(from, to, *caps)) {
			for (Session *s : members) {
				s->effective = wanted[s->id];
				result.retuned.push_back(s->id);
				Q_EMIT destinationChanged(QString::fromStdString(s->id));
			}
		} else {
			for (Session *s : members)
				restart.push_back(s->id);
		}
	}

	for (const std::string &id : restart) {
		if (allowReconnect) {
			reconnect(id);
			result.reconnecting.push_back(id);
		} else {
			result.pending.push_back(id);
		}
	}
	return result;
}

void OutputManager::stopAll()
{
	std::vector<std::string> ids;
	for (const auto &entry : sessions_) {
		if (entry.second->state.canStop())
			ids.push_back(entry.first);
	}
	for (const std::string &id : ids)
		stop(id);
}

void OutputManager::forget(const std::string &id)
{
	Session *s = findSession(id);
	if (!s)
		return;

	// Callers stop a destination before they remove it, so there is normally no output here.
	// Handle the other case anyway: disconnect, force the output closed, then release.
	if (s->output) {
		s->startSignal.Disconnect();
		s->stopSignal.Disconnect();
		s->reconnectSignal.Disconnect();
		s->reconnectSuccessSignal.Disconnect();
		joinStopThreadsFor(id);
		obs_output_force_stop(s->output);
	}
	releaseObsObjects(*s);
	sessions_.erase(id);
}

void OutputManager::shutdown()
{
	if (shutDown_)
		return;
	shutDown_ = true;
	sampleTimer_.stop();

	// No more callbacks into this object.
	for (auto &entry : sessions_) {
		Session &s = *entry.second;
		s.startSignal.Disconnect();
		s.stopSignal.Disconnect();
		s.reconnectSignal.Disconnect();
		s.reconnectSuccessSignal.Disconnect();
	}

	// Workers hold output references. Let them finish before forcing anything.
	joinFinishedStopThreads(true);

	int stopped = 0;
	for (auto &entry : sessions_) {
		Session &s = *entry.second;
		if (s.output) {
			obs_output_force_stop(s.output);
			++stopped;
		}
		releaseObsObjects(s);
	}
	sessions_.clear();

	if (stopped > 0)
		logInfo("Stopped {} output(s) for shutdown.", stopped);
}

// ---- Signals from OBS ----------------------------------------------------------------------------
// These run on OBS threads. They copy plain values and post to the UI thread. Nothing else.

void OutputManager::onOutputStart(void *data, calldata_t *)
{
	const auto *context = static_cast<const SignalContext *>(data);
	OutputManager *manager = context->manager;
	QMetaObject::invokeMethod(
		manager, [manager, id = context->id, generation = context->generation]() { manager->handleStarted(id, generation); },
		Qt::QueuedConnection);
}

void OutputManager::onOutputStop(void *data, calldata_t *params)
{
	const auto *context = static_cast<const SignalContext *>(data);
	OutputManager *manager = context->manager;

	const int code = static_cast<int>(calldata_int(params, "code"));
	std::string obsError;
	if (auto *output = static_cast<obs_output_t *>(calldata_ptr(params, "output"))) {
		const char *lastError = obs_output_get_last_error(output);
		if (lastError)
			obsError = lastError;
	}

	QMetaObject::invokeMethod(
		manager,
		[manager, id = context->id, generation = context->generation, code, obsError]() {
			manager->handleStopped(id, generation, code, obsError);
		},
		Qt::QueuedConnection);
}

void OutputManager::onOutputReconnect(void *data, calldata_t *params)
{
	const auto *context = static_cast<const SignalContext *>(data);
	OutputManager *manager = context->manager;
	const int retryInSec = static_cast<int>(calldata_int(params, "timeout_sec"));
	QMetaObject::invokeMethod(
		manager,
		[manager, id = context->id, generation = context->generation, retryInSec]() {
			manager->handleReconnecting(id, generation, retryInSec);
		},
		Qt::QueuedConnection);
}

void OutputManager::onOutputReconnectSuccess(void *data, calldata_t *)
{
	const auto *context = static_cast<const SignalContext *>(data);
	OutputManager *manager = context->manager;
	QMetaObject::invokeMethod(
		manager, [manager, id = context->id, generation = context->generation]() { manager->handleReconnected(id, generation); },
		Qt::QueuedConnection);
}

void OutputManager::handleStarted(const std::string &id, uint64_t generation)
{
	Session *s = findSession(id);
	if (!s || s->generation != generation)
		return;
	if (s->state.outputStarted()) {
		logInfo("{} is live.", s->name);
		Q_EMIT destinationChanged(QString::fromStdString(id));
	}
}

void OutputManager::handleReconnecting(const std::string &id, uint64_t generation, int retryInSec)
{
	Session *s = findSession(id);
	if (!s || s->generation != generation)
		return;
	if (s->state.outputReconnecting(retryInSec)) {
		logWarning("{} lost its connection. Retry {} in {} s.", s->name, s->state.runtime().reconnectAttempt,
			   retryInSec);
		Q_EMIT destinationChanged(QString::fromStdString(id));
	}
}

void OutputManager::handleReconnected(const std::string &id, uint64_t generation)
{
	Session *s = findSession(id);
	if (!s || s->generation != generation)
		return;
	if (s->state.outputReconnected()) {
		logInfo("{} reconnected.", s->name);
		Q_EMIT destinationChanged(QString::fromStdString(id));
	}
}

void OutputManager::handleStopped(const std::string &id, uint64_t generation, int code, const std::string &obsError)
{
	Session *s = findSession(id);
	if (!s || s->generation != generation)
		return;

	const StopReason reason = stopReasonFromObsCode(code);
	const bool wasPrivateTest = s->state.runtime().privateTest;

	UserMessage message;
	if (const IProvider *provider = app_.providers().find(s->providerId)) {
		message = provider->describeStop(s->name, reason, obsError);
	} else if (reason != StopReason::UserStopped) {
		message.what = locf("Stop.Error", "{0} stopped because of an output error.", s->name);
	}

	// The output is finished. Wait for this destination's stop worker, which holds a reference
	// to the output and is about to return, then let go of the output and the encoders. After
	// that nothing uses the video source any more and a restart below gets fresh objects.
	joinStopThreadsFor(id);
	releaseObsObjects(*s);

	const DestinationStateMachine::StopOutcome outcome = s->state.outputStopped(reason, message);
	if (!outcome.changed)
		return;

	if (s->state.runtime().phase == DestinationPhase::Failed)
		logWarning("{} stopped ({}). {}", s->name, stopReasonName(reason), message.text());
	else
		logInfo("{} stopped.", s->name);

	Q_EMIT destinationChanged(QString::fromStdString(id));

	if (outcome.restart && !shutDown_)
		start(id, wasPrivateTest);
}

// ---- Stats -------------------------------------------------------------------------------------

void OutputManager::setSampleIntervalMs(int intervalMs)
{
	sampleTimer_.setInterval(std::clamp(intervalMs, 500, 10000));
}

void OutputManager::sample()
{
	joinFinishedStopThreads(false);

	const int64_t now = app_.clock().nowMs();
	bool any = false;

	for (auto &entry : sessions_) {
		Session &s = *entry.second;
		const DestinationPhase phase = s.state.runtime().phase;
		if (!s.output || (phase != DestinationPhase::Live && phase != DestinationPhase::Reconnecting))
			continue;
		any = true;

		const uint64_t bytes = obs_output_get_total_bytes(s.output);
		const int totalFrames = obs_output_get_total_frames(s.output);
		const int droppedFrames = obs_output_get_frames_dropped(s.output);

		DestinationStats &stats = s.stats;
		if (s.lastSampleMs > 0 && now > s.lastSampleMs) {
			const double seconds = (now - s.lastSampleMs) / 1000.0;
			// The byte counter restarts after a reconnect.
			const uint64_t delta = bytes >= s.lastBytes ? bytes - s.lastBytes : bytes;
			stats.bitrateKbps = static_cast<int>(delta * 8.0 / 1000.0 / seconds);

			const int frames = totalFrames - s.lastTotalFrames;
			const int dropped = droppedFrames - s.lastDroppedFrames;
			stats.recentDroppedPercent = frames > 0 && dropped > 0 ? 100.0 * dropped / frames : 0.0;
		}

		stats.totalBytes = bytes;
		stats.totalFrames = totalFrames;
		stats.droppedFrames = droppedFrames;
		stats.droppedPercent = totalFrames > 0 ? 100.0 * droppedFrames / totalFrames : 0.0;
		stats.congestion = obs_output_get_congestion(s.output);
		stats.connectTimeMs = obs_output_get_connect_time_ms(s.output);
		stats.liveSeconds = s.state.liveSeconds();
		if (phase == DestinationPhase::Reconnecting)
			stats.bitrateKbps = 0;

		s.lastBytes = bytes;
		s.lastTotalFrames = totalFrames;
		s.lastDroppedFrames = droppedFrames;
		s.lastSampleMs = now;
	}

	if (any)
		Q_EMIT statsUpdated();
}

// ---- State -------------------------------------------------------------------------------------

DestinationRuntime OutputManager::runtime(const std::string &id) const
{
	const Session *s = findSession(id);
	return s ? s->state.runtime() : DestinationRuntime{};
}

DestinationStats OutputManager::stats(const std::string &id) const
{
	const Session *s = findSession(id);
	return s ? s->stats : DestinationStats{};
}

DestinationSessionInfo OutputManager::sessionInfo(const std::string &id) const
{
	DestinationSessionInfo info;
	const Session *s = findSession(id);
	if (!s)
		return info;

	info.effective = s->effective;
	info.hasOutput = static_cast<bool>(s->output);
	if (s->videoEncoder) {
		info.videoEncoderName = obs_encoder_get_name(s->videoEncoder);
		for (const auto &entry : sessions_) {
			const Session &other = *entry.second;
			if (other.id != id && other.videoEncoder.Get() == s->videoEncoder.Get())
				info.sharedWith.push_back(other.id);
		}
	}
	if (s->audioEncoder)
		info.audioEncoderName = obs_encoder_get_name(s->audioEncoder);
	return info;
}

bool OutputManager::anyActive() const
{
	return std::any_of(sessions_.begin(), sessions_.end(),
			   [](const auto &entry) { return entry.second->state.runtime().active(); });
}

int OutputManager::liveCount() const
{
	return static_cast<int>(std::count_if(sessions_.begin(), sessions_.end(), [](const auto &entry) {
		return entry.second->state.runtime().phase == DestinationPhase::Live;
	}));
}

int OutputManager::totalBitrateKbps() const
{
	int total = 0;
	for (const auto &entry : sessions_) {
		if (entry.second->state.runtime().phase == DestinationPhase::Live)
			total += entry.second->stats.bitrateKbps;
	}
	return total;
}

} // namespace rd
