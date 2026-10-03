// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "testing/scenario_runner.h"

#include "app/app_context.h"
#include "outputs/output_manager.h"
#include "outputs/vertical_canvas.h"
#include "settings/config_json.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <graphics/vec4.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <QImage>
#include <QMainWindow>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace rd {

namespace {

using json = nlohmann::json;

std::string env(const char *name)
{
	const char *value = std::getenv(name);
	return value ? value : "";
}

std::string text(const json &object, const char *key, const std::string &fallback = {})
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

double number(const json &object, const char *key, double fallback)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_number() ? it->get<double>() : fallback;
}

bool flag(const json &object, const char *key, bool fallback)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

// Renders a source to an image, on the OBS graphics thread, the way OBS takes a screenshot.
struct RenderRequest {
	obs_source_t *source = nullptr;
	int width = 0;
	int height = 0;
	QImage image;
	bool ok = false;
};

void renderSourceTask(void *param)
{
	auto *request = static_cast<RenderRequest *>(param);
	obs_enter_graphics();

	gs_texrender_t *texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(static_cast<uint32_t>(request->width),
						       static_cast<uint32_t>(request->height), GS_RGBA);

	if (gs_texrender_begin(texrender, static_cast<uint32_t>(request->width), static_cast<uint32_t>(request->height))) {
		vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(request->width), 0.0f, static_cast<float>(request->height), -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_inc_showing(request->source);
		obs_source_video_render(request->source);
		obs_source_dec_showing(request->source);
		gs_blend_state_pop();
		gs_texrender_end(texrender);

		gs_stage_texture(stage, gs_texrender_get_texture(texrender));
		uint8_t *data = nullptr;
		uint32_t lineSize = 0;
		if (gs_stagesurface_map(stage, &data, &lineSize)) {
			QImage image(request->width, request->height, QImage::Format_RGBA8888);
			for (int y = 0; y < request->height; ++y)
				memcpy(image.scanLine(y), data + static_cast<size_t>(y) * lineSize,
				       static_cast<size_t>(request->width) * 4);
			gs_stagesurface_unmap(stage);
			request->image = image;
			request->ok = true;
		}
	}

	gs_stagesurface_destroy(stage);
	gs_texrender_destroy(texrender);
	obs_leave_graphics();
}

// "#RRGGBB" to red, green, blue.
bool parseColor(const std::string &hex, int &r, int &g, int &b)
{
	if (hex.size() != 7 || hex[0] != '#')
		return false;
	try {
		r = std::stoi(hex.substr(1, 2), nullptr, 16);
		g = std::stoi(hex.substr(3, 2), nullptr, 16);
		b = std::stoi(hex.substr(5, 2), nullptr, 16);
	} catch (const std::exception &) {
		return false;
	}
	return true;
}

// The bounding box of every pixel within `tolerance` of a colour.
json measureColor(const QImage &image, int r, int g, int b, int tolerance)
{
	int left = image.width();
	int top = image.height();
	int right = -1;
	int bottom = -1;
	long long count = 0;
	for (int y = 0; y < image.height(); ++y) {
		const uchar *line = image.constScanLine(y);
		for (int x = 0; x < image.width(); ++x) {
			const uchar *pixel = line + x * 4;
			if (std::abs(pixel[0] - r) <= tolerance && std::abs(pixel[1] - g) <= tolerance &&
			    std::abs(pixel[2] - b) <= tolerance) {
				++count;
				left = std::min(left, x);
				right = std::max(right, x);
				top = std::min(top, y);
				bottom = std::max(bottom, y);
			}
		}
	}
	json out;
	out["pixels"] = count;
	if (count > 0) {
		out["x"] = left;
		out["y"] = top;
		out["width"] = right - left + 1;
		out["height"] = bottom - top + 1;
	}
	return out;
}

} // namespace

ScenarioRunner::ScenarioRunner(AppContext &app, QObject *parent) : QObject(parent), app_(app)
{
	timer_.setInterval(100);
	connect(&timer_, &QTimer::timeout, this, &ScenarioRunner::tick);
	cpu_ = os_cpu_usage_info_start();
}

ScenarioRunner::~ScenarioRunner()
{
	if (cpu_)
		os_cpu_usage_info_destroy(cpu_);
}

bool ScenarioRunner::startFromEnvironment()
{
	const std::string scenarioPath = env("RELAYDOCK_SCENARIO");
	if (scenarioPath.empty())
		return false;
	resultPath_ = env("RELAYDOCK_SCENARIO_RESULT");

	std::string content;
	if (!readFileToString(pathFromUtf8(scenarioPath), content, 4 * 1024 * 1024)) {
		logError("Scenario: could not read {}", scenarioPath);
		return false;
	}

	const json root = json::parse(content, nullptr, false, true);
	if (root.is_discarded() || !root.is_object() || !root.contains("steps") || !root["steps"].is_array()) {
		logError("Scenario: {} is not a scenario file.", scenarioPath);
		return false;
	}

	steps_ = root["steps"];
	results_ = {{"scenario", scenarioPath},
		    {"completed", false},
		    {"failure", nullptr},
		    {"steps", json::array()},
		    {"snapshots", json::object()}};
	scenarioStartedMs_ = app_.clock().nowMs();

	logInfo("Scenario: running {} step(s) from {}", steps_.size(), scenarioPath);
	writeResults();
	timer_.start();
	return true;
}

void ScenarioRunner::tick()
{
	if (finished_)
		return;

	// The timer fires every 100 ms on the OBS UI thread. A longer gap means something
	// blocked that thread, which is exactly what RelayDock must never do.
	const int64_t tickTimeMs = app_.clock().nowMs();
	if (lastTickMs_ > 0 && tickTimeMs - lastTickMs_ > maxTickGapMs_)
		maxTickGapMs_ = tickTimeMs - lastTickMs_;
	lastTickMs_ = tickTimeMs;

	// Run steps until one has to wait. One tick can run many instant steps.
	while (index_ < steps_.size()) {
		const json &step = steps_[index_];
		std::string detail;
		StepResult result;

		if (!waiting_) {
			stepStartedMs_ = app_.clock().nowMs();
			result = beginStep(step, detail);
		} else {
			result = pollStep(step, detail);
		}

		if (result == StepResult::Waiting) {
			waiting_ = true;
			return;
		}

		waiting_ = false;
		recordStep(step, result == StepResult::Done, detail);
		if (result == StepResult::Failed) {
			finish(false, "Step " + std::to_string(index_) + " (" + text(step, "op") + ") failed: " + detail);
			return;
		}

		++index_;
		if (finished_)
			return;
	}

	finish(true, {});
}

std::string ScenarioRunner::idFor(const json &step, std::string &detail) const
{
	const std::string ref = text(step, "ref");
	const auto it = refs_.find(ref);
	if (it == refs_.end()) {
		detail = "Unknown destination ref '" + ref + "'.";
		return {};
	}
	return it->second;
}

ScenarioRunner::StepResult ScenarioRunner::beginStep(const json &step, std::string &detail)
{
	const std::string op = text(step, "op");

	if (op == "clear") {
		const std::vector<DestinationConfig> all = app_.config().destinations;
		for (const DestinationConfig &destination : all)
			app_.removeDestination(destination.id);
		refs_.clear();
		app_.notifyConfigChanged();
		return StepResult::Done;
	}

	if (op == "add_destination") {
		DestinationConfig *destination = app_.addDestination(text(step, "provider"));
		if (!destination) {
			detail = "Unknown provider '" + text(step, "provider") + "'.";
			return StepResult::Failed;
		}

		// Apply the "config" object on top of the provider defaults.
		if (step.contains("config") && step["config"].is_object()) {
			json merged = json::parse(serializeDestination(*destination));
			merged.merge_patch(step["config"]);
			DestinationConfig patched;
			if (!parseDestination(merged.dump(), patched)) {
				detail = "The config object could not be applied.";
				return StepResult::Failed;
			}
			patched.id = destination->id;
			patched.provider = destination->provider;
			*destination = patched;
		}

		const std::string id = destination->id;
		refs_[text(step, "ref", destination->name)] = id;

		const std::string key = text(step, "stream_key");
		if (!key.empty())
			app_.vault().set({id, CredentialKind::StreamKey}, SecretString(key));
		const std::string password = text(step, "password");
		if (!password.empty())
			app_.vault().set({id, CredentialKind::Password}, SecretString(password));

		app_.notifyConfigChanged();
		detail = id;
		return StepResult::Done;
	}

	if (op == "set") {
		const std::string mode = text(step, "performance_mode");
		if (!mode.empty() && !performanceModeFromName(mode, app_.config().performanceMode)) {
			detail = "Unknown performance mode '" + mode + "'.";
			return StepResult::Failed;
		}
		if (step.contains("enabled") && step["enabled"].is_object()) {
			for (const auto &entry : step["enabled"].items()) {
				const auto ref = refs_.find(entry.key());
				DestinationConfig *destination =
					ref == refs_.end() ? nullptr : app_.config().findDestination(ref->second);
				if (!destination || !entry.value().is_boolean()) {
					detail = "Unknown destination ref '" + entry.key() + "'.";
					return StepResult::Failed;
				}
				destination->enabled = entry.value().get<bool>();
			}
		}
		app_.notifyConfigChanged();
		return StepResult::Done;
	}

	if (op == "start") {
		const std::string id = idFor(step, detail);
		if (id.empty())
			return StepResult::Failed;
		if (!app_.outputs().start(id, flag(step, "private_test", false))) {
			detail = "The destination was not idle.";
			return StepResult::Failed;
		}
		return StepResult::Done;
	}

	if (op == "start_all") {
		detail = std::to_string(app_.outputs().startAllEnabled()) + " started";
		return StepResult::Done;
	}

	if (op == "stop") {
		const std::string id = idFor(step, detail);
		if (id.empty())
			return StepResult::Failed;
		app_.outputs().stop(id);
		return StepResult::Done;
	}

	if (op == "stop_all") {
		app_.outputs().stopAll();
		return StepResult::Done;
	}

	if (op == "reconnect") {
		const std::string id = idFor(step, detail);
		if (id.empty())
			return StepResult::Failed;
		app_.outputs().reconnect(id);
		return StepResult::Done;
	}

	if (op == "snapshot") {
		results_["snapshots"][text(step, "label", "snapshot-" + std::to_string(index_))] = snapshot();
		return StepResult::Done;
	}

	if (op == "obs_video") {
		// Sets the OBS canvas, output size and frame rate, so a test does not depend on the
		// monitor of the PC it runs on.
		config_t *profile = obs_frontend_get_profile_config();
		const int baseWidth = static_cast<int>(number(step, "base_width", 1920));
		const int baseHeight = static_cast<int>(number(step, "base_height", 1080));
		config_set_uint(profile, "Video", "BaseCX", static_cast<uint64_t>(baseWidth));
		config_set_uint(profile, "Video", "BaseCY", static_cast<uint64_t>(baseHeight));
		config_set_uint(profile, "Video", "OutputCX", static_cast<uint64_t>(number(step, "output_width", baseWidth)));
		config_set_uint(profile, "Video", "OutputCY", static_cast<uint64_t>(number(step, "output_height", baseHeight)));
		config_set_uint(profile, "Video", "FPSType", 0);
		config_set_string(profile, "Video", "FPSCommon", text(step, "fps", "60").c_str());
		config_save(profile);
		obs_frontend_reset_video();

		obs_video_info ovi{};
		obs_get_video_info(&ovi);
		detail = std::to_string(ovi.base_width) + "x" + std::to_string(ovi.base_height) + " base, " +
			 std::to_string(ovi.output_width) + "x" + std::to_string(ovi.output_height) + " output, " +
			 std::to_string(ovi.fps_num) + "/" + std::to_string(ovi.fps_den) + " FPS";
		if (static_cast<int>(ovi.base_width) != baseWidth || static_cast<int>(ovi.base_height) != baseHeight) {
			detail = "OBS did not apply the video settings: " + detail;
			return StepResult::Failed;
		}
		return StepResult::Done;
	}

	if (op == "add_color_source") {
		// Adds a plain coloured rectangle to the current OBS scene. Tests use it to put a
		// known picture on screen without capturing anything from the PC.
		int r = 0;
		int g = 0;
		int b = 0;
		if (!parseColor(text(step, "color", "#FF0000"), r, g, b)) {
			detail = "color must be #RRGGBB.";
			return StepResult::Failed;
		}
		OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (!scene) {
			detail = "OBS has no current scene.";
			return StepResult::Failed;
		}

		OBSDataAutoRelease settings = obs_data_create();
		// OBS stores colours as 0xAABBGGRR.
		const long long abgr = 0xFF000000LL | (static_cast<long long>(b) << 16) | (static_cast<long long>(g) << 8) | r;
		obs_data_set_int(settings, "color", abgr);
		obs_data_set_int(settings, "width", static_cast<long long>(number(step, "width", 400)));
		obs_data_set_int(settings, "height", static_cast<long long>(number(step, "height", 400)));

		const std::string name = text(step, "name", "RelayDock test colour");
		OBSSourceAutoRelease source = obs_source_create("color_source_v3", name.c_str(), settings, nullptr);
		obs_sceneitem_t *item = source ? obs_scene_add(scene, source) : nullptr;
		if (!item) {
			detail = "OBS could not create the colour source.";
			return StepResult::Failed;
		}
		vec2 position;
		vec2_set(&position, static_cast<float>(number(step, "x", 0)), static_cast<float>(number(step, "y", 0)));
		obs_sceneitem_set_pos(item, &position);
		detail = obs_source_get_uuid(source);
		return StepResult::Done;
	}

	if (op == "clear_scene") {
		// Removes every item from the current OBS scene, so a test starts from a known picture.
		OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (!scene) {
			detail = "OBS has no current scene.";
			return StepResult::Failed;
		}
		std::vector<obs_sceneitem_t *> items;
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				static_cast<std::vector<obs_sceneitem_t *> *>(param)->push_back(item);
				return true;
			},
			&items);
		for (obs_sceneitem_t *item : items) {
			OBSSource source = obs_sceneitem_get_source(item);
			obs_sceneitem_remove(item);
			obs_source_remove(source);
		}
		detail = std::to_string(items.size()) + " item(s) removed";
		return StepResult::Done;
	}

	if (op == "vertical_layout") {
		// Replaces the vertical layouts with the ones given, in the saved-file format.
		std::vector<VerticalLayout> layouts;
		if (!step.contains("layouts") || !parseVerticalLayouts(json{{"layouts", step["layouts"]}}.dump(), layouts)) {
			detail = "layouts must be an array of vertical layouts.";
			return StepResult::Failed;
		}
		app_.vertical().setLayouts(layouts);
		detail = std::to_string(app_.vertical().layouts().size()) + " layout(s)";
		return StepResult::Done;
	}

	if (op == "render_vertical") {
		// Renders the vertical canvas to an image and measures where a colour lands on it.
		VerticalCanvas &canvas = app_.vertical().canvasFor(text(step, "layout_id"));
		canvas.sync(app_.vertical().layoutFor(text(step, "layout_id")));

		RenderRequest request;
		request.source = canvas.sceneSource();
		request.width = canvas.width();
		request.height = canvas.height();
		obs_queue_task(OBS_TASK_GRAPHICS, renderSourceTask, &request, true);
		if (!request.ok) {
			detail = "OBS could not render the vertical canvas.";
			return StepResult::Failed;
		}

		json measured;
		measured["width"] = request.width;
		measured["height"] = request.height;
		measured["missing_items"] = canvas.missingItems().size();
		if (step.contains("find") && step["find"].is_object()) {
			const int tolerance = static_cast<int>(number(step, "tolerance", 40));
			for (const auto &entry : step["find"].items()) {
				int r = 0;
				int g = 0;
				int b = 0;
				if (!entry.value().is_string() || !parseColor(entry.value().get<std::string>(), r, g, b)) {
					detail = "find values must be #RRGGBB.";
					return StepResult::Failed;
				}
				measured["colors"][entry.key()] = measureColor(request.image, r, g, b, tolerance);
			}
		}
		const std::string file = text(step, "file");
		if (!file.empty())
			measured["saved"] = request.image.save(QString::fromUtf8(file.c_str()), "PNG");

		results_["renders"][text(step, "label", "render-" + std::to_string(index_))] = measured;
		return StepResult::Done;
	}

	if (op == "save_config") {
		if (!app_.saveConfig()) {
			detail = "Saving failed.";
			return StepResult::Failed;
		}
		return StepResult::Done;
	}

	if (op == "quit") {
		finish(true, {});
		return StepResult::Done;
	}

	if (op == "wait" || op == "wait_phase" || op == "wait_reconnects" || op == "wait_idle")
		return pollStep(step, detail);

	detail = "Unknown op '" + op + "'.";
	return StepResult::Failed;
}

ScenarioRunner::StepResult ScenarioRunner::pollStep(const json &step, std::string &detail)
{
	const std::string op = text(step, "op");
	const double elapsedSec = (app_.clock().nowMs() - stepStartedMs_) / 1000.0;

	if (op == "wait")
		return elapsedSec >= number(step, "seconds", 1.0) ? StepResult::Done : StepResult::Waiting;

	const double timeoutSec = number(step, "timeout_sec", 30.0);

	if (op == "wait_idle") {
		// Every destination has stopped, whatever the reason.
		if (!app_.outputs().anyActive())
			return StepResult::Done;
		if (elapsedSec >= timeoutSec) {
			detail = "Some destinations were still active after the timeout.";
			return StepResult::Failed;
		}
		return StepResult::Waiting;
	}

	const std::string id = idFor(step, detail);
	if (id.empty())
		return StepResult::Failed;
	const DestinationRuntime runtime = app_.outputs().runtime(id);

	if (op == "wait_phase") {
		const std::string wanted = text(step, "phase");
		const std::string current = destinationPhaseName(runtime.phase);
		if (current == wanted) {
			detail = "reached after " + std::to_string(static_cast<int>(elapsedSec * 1000)) + " ms";
			return StepResult::Done;
		}
		// "failed" is final. Waiting for "live" after a failure would only run out the clock.
		if (runtime.phase == DestinationPhase::Failed && wanted != "failed") {
			detail = "The destination failed instead: " + runtime.error.text();
			return StepResult::Failed;
		}
		if (elapsedSec >= timeoutSec) {
			detail = "Still '" + current + "' after " + std::to_string(static_cast<int>(timeoutSec)) + " s, wanted '" +
				 wanted + "'.";
			return StepResult::Failed;
		}
		return StepResult::Waiting;
	}

	if (op == "wait_reconnects") {
		const int wanted = static_cast<int>(number(step, "count", 1));
		if (runtime.reconnectsThisRun >= wanted && runtime.phase == DestinationPhase::Live)
			return StepResult::Done;
		if (runtime.phase == DestinationPhase::Failed || runtime.phase == DestinationPhase::Idle) {
			detail = "The destination stopped before it reconnected: " + runtime.error.text();
			return StepResult::Failed;
		}
		if (elapsedSec >= timeoutSec) {
			detail = "Only " + std::to_string(runtime.reconnectsThisRun) + " reconnect(s) after the timeout.";
			return StepResult::Failed;
		}
		return StepResult::Waiting;
	}

	detail = "Unknown op '" + op + "'.";
	return StepResult::Failed;
}

json ScenarioRunner::snapshot()
{
	json out;
	out["time_ms"] = app_.clock().nowMs() - scenarioStartedMs_;
	// Longest stall of the OBS UI thread since the previous snapshot.
	out["max_ui_gap_ms"] = maxTickGapMs_;
	maxTickGapMs_ = 0;

	std::map<std::string, std::string> refOfId;
	for (const auto &entry : refs_)
		refOfId[entry.second] = entry.first;

	json destinations = json::object();
	for (const auto &entry : refs_) {
		const std::string &id = entry.second;
		const DestinationRuntime runtime = app_.outputs().runtime(id);
		const DestinationStats stats = app_.outputs().stats(id);
		const DestinationSessionInfo info = app_.outputs().sessionInfo(id);

		json shared = json::array();
		for (const std::string &other : info.sharedWith) {
			const auto ref = refOfId.find(other);
			shared.push_back(ref == refOfId.end() ? other : ref->second);
		}

		const EffectiveVideo &video = info.effective.video;
		destinations[entry.first] = {
			{"id", id},
			{"phase", destinationPhaseName(runtime.phase)},
			{"error", runtime.error.text()},
			{"last_stop", stopReasonName(runtime.lastStop)},
			{"private_test", runtime.privateTest},
			{"reconnect_attempt", runtime.reconnectAttempt},
			{"reconnects", runtime.reconnectsThisRun},
			{"has_output", info.hasOutput},
			{"video_encoder", info.videoEncoderName},
			{"audio_encoder", info.audioEncoderName},
			{"shared_with", shared},
			{"stats",
			 {{"total_bytes", stats.totalBytes},
			  {"bitrate_kbps", stats.bitrateKbps},
			  {"total_frames", stats.totalFrames},
			  {"dropped_frames", stats.droppedFrames},
			  {"dropped_percent", stats.droppedPercent},
			  {"congestion", stats.congestion},
			  {"connect_ms", stats.connectTimeMs},
			  {"live_seconds", stats.liveSeconds}}},
			{"effective",
			 {{"orientation", orientationName(video.orientation)},
			  {"width", video.width},
			  {"height", video.height},
			  {"fps", video.fps},
			  {"fps_divisor", video.fpsDivisor},
			  {"encoder", video.encoderId},
			  {"bitrate_kbps", video.bitrateKbps},
			  {"rate_control", video.rateControl},
			  {"preset", video.preset},
			  {"keyframe_interval_sec", video.keyframeIntervalSec},
			  {"audio_encoder", info.effective.audio.encoderId},
			  {"audio_bitrate_kbps", info.effective.audio.bitrateKbps}}},
		};
	}
	out["destinations"] = std::move(destinations);

	out["encoders"] = {{"video_live", app_.outputs().encoderPool().liveVideoEncoders()},
			   {"audio_live", app_.outputs().encoderPool().liveAudioEncoders()}};

	video_t *video = obs_get_video();
	out["obs"] = {
		{"version", obs_get_version_string()},
		{"render_total_frames", obs_get_total_frames()},
		{"render_lagged_frames", obs_get_lagged_frames()},
		{"encode_total_frames", video ? video_output_get_total_frames(video) : 0},
		{"encode_skipped_frames", video ? video_output_get_skipped_frames(video) : 0},
		{"active_fps", obs_get_active_fps()},
		{"average_frame_time_ms", obs_get_average_frame_time_ns() / 1000000.0},
		{"memory_mb", os_get_proc_resident_size() / (1024.0 * 1024.0)},
		{"cpu_percent", cpu_ ? os_cpu_usage_info_query(cpu_) : 0.0},
		{"live_destinations", app_.outputs().liveCount()},
		{"total_bitrate_kbps", app_.outputs().totalBitrateKbps()},
	};
	return out;
}

void ScenarioRunner::recordStep(const json &step, bool ok, const std::string &detail)
{
	results_["steps"].push_back({{"index", index_},
				     {"op", text(step, "op")},
				     {"ref", text(step, "ref")},
				     {"ok", ok},
				     {"detail", detail},
				     {"elapsed_ms", app_.clock().nowMs() - stepStartedMs_}});
	writeResults();
}

void ScenarioRunner::writeResults()
{
	if (resultPath_.empty())
		return;
	std::string error;
	if (!writeFileAtomically(pathFromUtf8(resultPath_), results_.dump(2) + "\n", error))
		logError("Scenario: could not write the result file. {}", error);
}

void ScenarioRunner::finish(bool ok, const std::string &failure)
{
	if (finished_)
		return;
	finished_ = true;
	timer_.stop();

	results_["completed"] = ok;
	if (!ok) {
		results_["failure"] = failure;
		results_["snapshots"]["at_failure"] = snapshot();
		logError("Scenario: {}", failure);
	} else {
		logInfo("Scenario: finished.");
	}
	writeResults();

	// Test runs use their own credential prefix. Leave nothing behind in it.
	if (!env("RELAYDOCK_TEST_CREDENTIAL_PREFIX").empty())
		app_.vault().removeAll();

	if (auto *window = static_cast<QMainWindow *>(obs_frontend_get_main_window()))
		QMetaObject::invokeMethod(window, "close", Qt::QueuedConnection);
}

} // namespace rd
