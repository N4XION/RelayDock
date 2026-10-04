// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "testing/scenario_runner.h"

#include "app/app_context.h"
#include "app/chat_hub.h"
#include "chat/chat_accounts.h"
#include "app/diagnostics_service.h"
#include "app/performance_monitor.h"
#include "app/update_install.h"
#include "build_info.h"
#include "legal/legal_documents.h"
#include "utils/clock.h"
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

#include <QAbstractButton>
#include <QApplication>
#include <QImage>
#include <QMainWindow>
#include <QMessageBox>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <windows.h>

#include <psapi.h>

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

const char *updateStateId(UpdateInstall::State state)
{
	switch (state) {
	case UpdateInstall::State::Idle:
		return "idle";
	case UpdateInstall::State::Downloading:
		return "downloading";
	case UpdateInstall::State::Waiting:
		return "waiting";
	case UpdateInstall::State::Failed:
		return "failed";
	}
	return "idle";
}

bool flag(const json &object, const char *key, bool fallback)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

json lockState(const DestinationConfig *config)
{
	if (!config)
		return json::object();
	return {{"resolution", config->locks.resolution}, {"fps", config->locks.fps}, {"bitrate", config->locks.bitrate}};
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
	// A test that restarts OBS to check that keys survive asks the first run to leave them.
	keepCredentials_ = flag(root, "keep_credentials", false);
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
		if (step.contains("optimizer") && step["optimizer"].is_object()) {
			const json &optimizer = step["optimizer"];
			OptimizerConfig &config = app_.config().optimizer;
			const std::string optimizerMode = text(optimizer, "mode");
			if (!optimizerMode.empty() && !optimizationModeFromName(optimizerMode, config.mode)) {
				detail = "Unknown optimisation mode '" + optimizerMode + "'.";
				return StepResult::Failed;
			}
			config.preventGameLag = flag(optimizer, "prevent_game_lag", config.preventGameLag);
			config.allowReconnectingChanges = flag(optimizer, "allow_reconnecting_changes", config.allowReconnectingChanges);
		}
		if (step.contains("upload_kbps"))
			app_.config().network.uploadKbps = static_cast<int>(number(step, "upload_kbps", 0));
		app_.notifyConfigChanged();
		return StepResult::Done;
	}

	if (op == "optimizer_tuning") {
		// Shorter timers, so a test does not have to stream for ten minutes to see a recovery.
		OptimizerTuning tuning;
		tuning.sustainMs = static_cast<int64_t>(number(step, "sustain_ms", static_cast<double>(tuning.sustainMs)));
		tuning.sustainMsStrict = tuning.sustainMs;
		tuning.cooldownMs = static_cast<int64_t>(number(step, "cooldown_ms", static_cast<double>(tuning.cooldownMs)));
		tuning.recoverAfterMs =
			static_cast<int64_t>(number(step, "recover_after_ms", static_cast<double>(tuning.recoverAfterMs)));
		tuning.recoverAfterMsStrict = tuning.recoverAfterMs;
		tuning.probationMs = static_cast<int64_t>(number(step, "probation_ms", static_cast<double>(tuning.probationMs)));
		tuning.dropTrigger = number(step, "drop_trigger", tuning.dropTrigger);
		app_.performance().setTuningForTest(tuning);
		return StepResult::Done;
	}

	if (op == "suggestion") {
		const auto &suggestions = app_.performance().suggestions();
		if (suggestions.empty()) {
			detail = "There is no suggestion.";
			return StepResult::Failed;
		}
		const std::string id = suggestions.front().change.id;
		const std::string action = text(step, "action", "apply");
		detail = suggestions.front().text.title;
		if (action == "apply")
			return app_.performance().applySuggestion(id) ? StepResult::Done : StepResult::Failed;
		if (action == "lock")
			return app_.performance().lockSuggestion(id) ? StepResult::Done : StepResult::Failed;
		if (action == "ignore") {
			app_.performance().ignoreSuggestion(id);
			return StepResult::Done;
		}
		detail = "Unknown action '" + action + "'.";
		return StepResult::Failed;
	}

	if (op == "preflight") {
		const PreflightReport report = runPreflightNow(app_);
		json items = json::array();
		for (const PreflightItem &item : report.items) {
			items.push_back({{"id", item.id},
					 {"status", preflightStatusName(item.status)},
					 {"title", item.title},
					 {"text", item.message.text()}});
		}
		results_["preflight"][text(step, "label", "preflight-" + std::to_string(index_))] = {
			{"status", preflightStatusName(report.status)}, {"items", items}};
		detail = preflightStatusName(report.status);
		return StepResult::Done;
	}

	if (op == "diagnostics") {
		results_["diagnostics"][text(step, "label", "diagnostics-" + std::to_string(index_))] = buildDiagnosticsNow(app_);
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
		//
		// "expect": "refused" is for the opposite case. OBS turns a change of video settings
		// down while video is in use, and the step passes when it does. Ask for a size that
		// differs from the current one, or the step cannot tell. "any" accepts both.
		const std::string expect = text(step, "expect", "applied");
		config_t *profile = obs_frontend_get_profile_config();
		const uint64_t oldBaseCx = config_get_uint(profile, "Video", "BaseCX");
		const uint64_t oldBaseCy = config_get_uint(profile, "Video", "BaseCY");
		const uint64_t oldOutputCx = config_get_uint(profile, "Video", "OutputCX");
		const uint64_t oldOutputCy = config_get_uint(profile, "Video", "OutputCY");
		const uint64_t oldFpsType = config_get_uint(profile, "Video", "FPSType");
		const char *oldFpsText = config_get_string(profile, "Video", "FPSCommon");
		const std::string oldFps = oldFpsText ? oldFpsText : "";

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
		const bool applied = static_cast<int>(ovi.base_width) == baseWidth && static_cast<int>(ovi.base_height) == baseHeight;
		detail = std::to_string(ovi.base_width) + "x" + std::to_string(ovi.base_height) + " base, " +
			 std::to_string(ovi.output_width) + "x" + std::to_string(ovi.output_height) + " output, " +
			 std::to_string(ovi.fps_num) + "/" + std::to_string(ovi.fps_den) + " FPS";
		if (!applied) {
			// Leave the profile as it was, so a later reset does not pick these values up.
			config_set_uint(profile, "Video", "BaseCX", oldBaseCx);
			config_set_uint(profile, "Video", "BaseCY", oldBaseCy);
			config_set_uint(profile, "Video", "OutputCX", oldOutputCx);
			config_set_uint(profile, "Video", "OutputCY", oldOutputCy);
			config_set_uint(profile, "Video", "FPSType", oldFpsType);
			config_set_string(profile, "Video", "FPSCommon", oldFps.c_str());
			config_save(profile);
		}
		results_["video_changes"].push_back({{"applied", applied}, {"expect", expect}, {"video", detail}});

		if (expect == "applied" && !applied) {
			detail = "OBS did not apply the video settings: " + detail;
			return StepResult::Failed;
		}
		if (expect == "refused" && applied) {
			detail = "OBS applied the video settings, and the test expected it to refuse: " + detail;
			return StepResult::Failed;
		}
		detail = (applied ? "applied: " : "refused: ") + detail;
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

	if (op == "add_moving_picture") {
		// A picture that scrolls across the whole canvas. Performance tests use it with an
		// image of random noise, so the encoder has real work to do. A still picture costs an
		// encoder almost nothing and would make every number look better than it is.
		OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (!scene) {
			detail = "OBS has no current scene.";
			return StepResult::Failed;
		}
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "file", text(step, "file").c_str());
		const std::string name = text(step, "name", "RelayDock test picture");
		OBSSourceAutoRelease source = obs_source_create("image_source", name.c_str(), settings, nullptr);
		if (!source || obs_source_get_width(source) == 0) {
			detail = "OBS could not load the picture " + text(step, "file");
			return StepResult::Failed;
		}

		OBSDataAutoRelease scroll = obs_data_create();
		obs_data_set_double(scroll, "speed_x", number(step, "speed_x", 240.0));
		obs_data_set_double(scroll, "speed_y", number(step, "speed_y", 135.0));
		OBSSourceAutoRelease filter = obs_source_create("scroll_filter", "RelayDock test scroll", scroll, nullptr);
		if (!filter) {
			detail = "OBS has no scroll filter.";
			return StepResult::Failed;
		}
		obs_source_filter_add(source, filter);

		obs_sceneitem_t *item = obs_scene_add(scene, source);
		if (!item) {
			detail = "OBS could not add the picture to the scene.";
			return StepResult::Failed;
		}
		obs_video_info video{};
		obs_get_video_info(&video);
		vec2 bounds;
		vec2_set(&bounds, static_cast<float>(video.base_width), static_cast<float>(video.base_height));
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
		obs_sceneitem_set_bounds(item, &bounds);
		detail = std::to_string(obs_source_get_width(source)) + "x" + std::to_string(obs_source_get_height(source));
		return StepResult::Done;
	}

	if (op == "deselect_scene") {
		// OBS selects a source when it is added and draws editing guides around it in the
		// preview. A picture of the window looks cleaner without them.
		OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (!scene) {
			detail = "OBS has no current scene.";
			return StepResult::Failed;
		}
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *) {
				obs_sceneitem_select(item, false);
				return true;
			},
			nullptr);
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

	if (op == "vertical_load_saved") {
		// Loads vertical layouts the way a scene collection does: from the saved text, with
		// its format number, through the upgrade of older formats.
		if (!step.contains("saved") || !step["saved"].is_object()) {
			detail = "saved must be the object a scene collection stores.";
			return StepResult::Failed;
		}
		app_.vertical().loadText(step["saved"].dump());
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

	if (op == "chat_setup") {
		// Points chat at stand-ins for the platforms on this PC and shortens its waits.
		ChatHub::TestSetup setup;
		setup.twitch.auth = text(step, "twitch_auth", setup.twitch.auth);
		setup.twitch.api = text(step, "twitch_api", setup.twitch.api);
		setup.twitch.eventSub = text(step, "twitch_eventsub", setup.twitch.eventSub);
		setup.youtube.api = text(step, "youtube_api", setup.youtube.api);
		setup.retryFirstMs = static_cast<long long>(number(step, "retry_first_ms", 0));
		setup.retryMaxMs = static_cast<long long>(number(step, "retry_max_ms", 0));
		setup.signInPollMs = static_cast<long long>(number(step, "sign_in_poll_ms", 0));
		setup.keepaliveGraceMs = static_cast<long long>(number(step, "keepalive_grace_ms", 0));
		setup.youtubeMinPollMs = static_cast<long long>(number(step, "youtube_min_poll_ms", 0));
		setup.youtubeNotLiveRetryMs = static_cast<long long>(number(step, "youtube_not_live_retry_ms", 0));
		app_.chat().setTestSetup(setup);
		app_.chat().clearTimeline();
		return StepResult::Done;
	}

	if (op == "chat_twitch_saved") {
		// A Twitch sign-in that was made earlier: the saved token and the application id.
		app_.vault().set(twitchChatCredential(), SecretString(text(step, "refresh_token")));
		app_.config().chat.twitchClientId = text(step, "client_id");
		app_.config().chat.twitchLogin = text(step, "login");
		app_.config().chat.twitchEnabled = flag(step, "enabled", true);
		app_.notifyConfigChanged();
		app_.chat().apply();
		return StepResult::Done;
	}

	if (op == "chat_twitch_client") {
		app_.config().chat.twitchClientId = text(step, "client_id");
		app_.notifyConfigChanged();
		app_.chat().apply();
		return StepResult::Done;
	}

	if (op == "chat_youtube_key") {
		app_.chat().setYouTubeKey(SecretString(text(step, "key")));
		return StepResult::Done;
	}

	if (op == "chat_youtube_connect") {
		UserMessage problem;
		if (!app_.chat().connectYouTube(text(step, "video"), problem)) {
			detail = problem.text();
			return flag(step, "expect_refused", false) ? StepResult::Done : StepResult::Failed;
		}
		return StepResult::Done;
	}

	if (op == "chat_youtube_disconnect") {
		app_.chat().disconnectYouTube();
		return StepResult::Done;
	}

	if (op == "chat_clear") {
		app_.chat().clearTimeline();
		return StepResult::Done;
	}

	if (op == "accept_legal") {
		// For tests that are not about the first-run review itself.
		for (const LegalDocument &document : legalDocuments())
			recordLegalAcceptance(app_.config().legal, document.id, utcTimestampIso8601(), buildInfo().version);
		app_.notifyConfigChanged();
		return StepResult::Done;
	}

	if (op == "config_patch") {
		// Changes any saved setting, the way an edited config.json would.
		json current = json::parse(serializeConfig(app_.config()), nullptr, false);
		if (current.is_discarded() || !step.contains("patch")) {
			detail = "No patch given.";
			return StepResult::Failed;
		}
		current.merge_patch(step["patch"]);
		ConfigParseResult parsed = parseConfig(current.dump());
		if (!parsed.ok) {
			detail = parsed.error;
			return StepResult::Failed;
		}
		app_.config() = parsed.config;
		app_.notifyConfigChanged();
		return StepResult::Done;
	}

	if (op == "legal_state") {
		json records = json::array();
		for (const LegalAcceptance &record : app_.config().legal)
			records.push_back({{"document", record.documentId}, {"version", record.version}, {"accepted_at", record.acceptedAtUtc}, {"app_version", record.appVersion}});
		results_["legal"][text(step, "label", "legal-" + std::to_string(index_))] = {
			{"complete", legalComplete(app_.config().legal)}, {"pending", pendingLegalDocuments(app_.config().legal)}, {"records", records}};
		return StepResult::Done;
	}

	if (op == "save_config") {
		if (!app_.saveConfig()) {
			detail = "Saving failed.";
			return StepResult::Failed;
		}
		return StepResult::Done;
	}

	if (op == "obs_user_config") {
		// Sets a true or false value in the OBS user settings, the ones OBS keeps in user.ini.
		config_t *user = obs_frontend_get_user_config();
		const std::string section = text(step, "section", "General");
		const std::string name = text(step, "name", "");
		if (!user || name.empty()) {
			detail = "obs_user_config needs a name.";
			return StepResult::Failed;
		}
		config_set_bool(user, section.c_str(), name.c_str(), step.value("value", false));
		detail = section + "/" + name + " = " + (step.value("value", false) ? "true" : "false");
		return StepResult::Done;
	}

	if (op == "quit") {
		// "confirm" names the button to press when a question comes up while OBS closes.
		quitConfirmButton_ = text(step, "confirm", "");
		// "with" names a button of an open RelayDock window that closes OBS, to press it instead
		// of closing the OBS window.
		quitWithButton_ = text(step, "with", "");
		finish(true, {});
		return StepResult::Done;
	}

	if (op == "update_cancel") {
		// What Cancel update does in the interface.
		app_.updateInstall().cancel();
		return StepResult::Done;
	}

	if (op == "wait" || op == "wait_phase" || op == "wait_reconnects" || op == "wait_idle" || op == "wait_suggestion" ||
	    op == "wait_adjustment" || op == "chat_wait" || op == "chat_wait_events" || op == "update_wait")
		return pollStep(step, detail);

	StepResult uiResult = StepResult::Done;
	if (uiStep(op, step, uiResult, detail))
		return uiResult;

	detail = "Unknown op '" + op + "'.";
	return StepResult::Failed;
}

ScenarioRunner::StepResult ScenarioRunner::pollStep(const json &step, std::string &detail)
{
	const std::string op = text(step, "op");
	const double elapsedSec = (app_.clock().nowMs() - stepStartedMs_) / 1000.0;

	StepResult uiResult = StepResult::Done;
	if (uiStep(op, step, uiResult, detail))
		return uiResult;

	if (op == "wait")
		return elapsedSec >= number(step, "seconds", 1.0) ? StepResult::Done : StepResult::Waiting;

	const double timeoutSec = number(step, "timeout_sec", 30.0);

	if (op == "chat_wait") {
		// Waits until a platform's chat reader is in a state: off, not_set_up, connecting,
		// connected, waiting or stopped.
		const ChatPlatform platform = text(step, "platform") == "youtube" ? ChatPlatform::YouTube : ChatPlatform::Twitch;
		const std::string wanted = text(step, "state", "connected");
		const ChatStatus status = app_.chat().status(platform);
		if (chatStateId(status.state) == wanted) {
			detail = "reached after " + std::to_string(static_cast<int>(elapsedSec * 1000)) + " ms";
			return StepResult::Done;
		}
		if (elapsedSec >= timeoutSec) {
			detail = std::string("Still '") + chatStateId(status.state) + "' at the timeout, wanted '" + wanted + "'. " +
				 status.message.text();
			return StepResult::Failed;
		}
		return StepResult::Waiting;
	}

	if (op == "update_wait") {
		// Waits until Update now is in a state: idle, downloading, waiting or failed.
		const std::string wanted = text(step, "state", "waiting");
		const UpdateInstall &update = app_.updateInstall();
		if (updateStateId(update.state()) == wanted) {
			detail = "reached after " + std::to_string(static_cast<int>(elapsedSec * 1000)) + " ms";
			return StepResult::Done;
		}
		// A failure is final. Waiting longer for another state would only hide it.
		if (elapsedSec >= timeoutSec || update.state() == UpdateInstall::State::Failed) {
			detail = std::string("Update now is '") + updateStateId(update.state()) + "', wanted '" + wanted + "'. " +
				 update.problem().text();
			return StepResult::Failed;
		}
		return StepResult::Waiting;
	}

	if (op == "chat_wait_events") {
		const auto wanted = static_cast<uint64_t>(number(step, "count", 1));
		if (app_.chat().timeline().total() >= wanted)
			return StepResult::Done;
		if (elapsedSec >= timeoutSec) {
			detail = std::to_string(app_.chat().timeline().total()) + " event(s) at the timeout, wanted " + std::to_string(wanted) + ".";
			return StepResult::Failed;
		}
		return StepResult::Waiting;
	}

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

	if (op == "wait_suggestion") {
		if (!app_.performance().suggestions().empty()) {
			detail = app_.performance().suggestions().front().text.title + " (after " +
				 std::to_string(static_cast<int>(elapsedSec)) + " s)";
			return StepResult::Done;
		}
		if (elapsedSec >= timeoutSec) {
			detail = "No suggestion appeared before the timeout.";
			return StepResult::Failed;
		}
		return StepResult::Waiting;
	}

	const std::string id = idFor(step, detail);
	if (id.empty())
		return StepResult::Failed;
	const DestinationRuntime runtime = app_.outputs().runtime(id);

	if (op == "wait_adjustment") {
		// "reduced": the optimiser lowered something. "none": everything is back to normal.
		const bool wantReduced = text(step, "state", "reduced") == "reduced";
		const Adjustment adjustment = app_.adjustmentFor(id);
		if (adjustment.none() != wantReduced) {
			detail = "bitrate " + std::to_string(adjustment.bitratePercent) + " percent after " +
				 std::to_string(static_cast<int>(elapsedSec)) + " s";
			return StepResult::Done;
		}
		if (elapsedSec >= timeoutSec) {
			detail = wantReduced ? "The optimiser made no reduction before the timeout."
					     : "The reduction was still in force at the timeout.";
			return StepResult::Failed;
		}
		return StepResult::Waiting;
	}

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

namespace {

// Memory the process has allocated for itself, in MB. Unlike the working set, Windows does not
// shrink this number when OBS sits minimised, so it is the one to watch for a leak.
double privateMemoryMb()
{
	PROCESS_MEMORY_COUNTERS_EX counters{};
	counters.cb = sizeof(counters);
	if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)))
		return 0.0;
	return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
}

} // namespace

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
		const Adjustment adjustment = app_.adjustmentFor(id);
		destinations[entry.first] = {
			{"id", id},
			{"adjustment",
			 {{"bitrate_percent", adjustment.bitratePercent}, {"max_fps", adjustment.maxFps}, {"max_lines", adjustment.maxLines}}},
			{"window_drop_percent", app_.performance().dropPercent(id)},
			{"locks", lockState(app_.config().findDestination(id))},
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

	const PerformanceSnapshot &performance = app_.performance().snapshot();
	out["performance"] = {
		{"obs_cpu_percent", performance.obsCpuPercent},   {"system_cpu_percent", performance.systemCpuPercent},
		{"gpu_percent", performance.gpuPercent},           {"memory_mb", performance.memoryMb},
		{"render_lag_percent", performance.renderLagPercent}, {"encode_lag_percent", performance.encodeLagPercent},
		{"worst_drop_percent", performance.worstDropPercent}, {"tick_interval_ms", app_.performance().tickIntervalMs()},
	};
	json suggestions = json::array();
	for (const Suggestion &suggestion : app_.performance().suggestions()) {
		suggestions.push_back({{"title", suggestion.text.title},
				       {"reason", suggestion.text.reason},
				       {"effect", suggestion.text.effect},
				       {"kind", optimizerChangeKindName(suggestion.change.kind)},
				       {"cause", optimizerCauseName(suggestion.change.cause)},
				       {"needs_reconnect", suggestion.change.needsReconnect}});
	}
	out["suggestions"] = std::move(suggestions);
	json history = json::array();
	for (const AppliedChange &applied : app_.performance().history())
		history.push_back({{"text", applied.text}, {"automatic", applied.automatic}});
	out["optimizer_history"] = std::move(history);

	out["keeps_awake"] = app_.keepsAwake();

	{
		// Chat: what each reader is doing, and the newest events. A test puts made-up comments
		// in, so they may be reported. A real chat is never written anywhere.
		ChatHub &chat = app_.chat();
		json platforms = json::object();
		for (const ChatPlatform platform : {ChatPlatform::Twitch, ChatPlatform::YouTube}) {
			const ChatStatus status = chat.status(platform);
			platforms[chatPlatformId(platform)] = {{"state", chatStateId(status.state)},
							       {"account", status.account},
							       {"message", status.message.text()}};
		}
		json events = json::array();
		const auto &all = chat.timeline().events();
		const size_t first = all.size() > 50 ? all.size() - 50 : 0;
		for (size_t i = first; i < all.size(); ++i) {
			const ChatEvent &event = all[i];
			events.push_back({{"platform", chatPlatformId(event.platform)},
					  {"kind", chatEventKindId(event.kind)},
					  {"id", event.id},
					  {"author", event.author},
					  {"text", event.text},
					  {"headline", event.headline}});
		}
		out["chat"] = {{"platforms", platforms},
			       {"events", events},
			       {"total", chat.timeline().total()},
			       {"twitch_signed_in", chat.twitchSignedIn()},
			       {"youtube_key_saved", chat.youtubeKeySaved()},
			       {"youtube_requests", chat.youtubeRequests()}};
	}

	{
		const UpdateInstall &update = app_.updateInstall();
		out["update"] = {{"state", updateStateId(update.state())},
				 {"installed_by_installer", update.installedByInstaller()},
				 {"received", update.received()},
				 {"total", update.total()},
				 {"problem", update.problem().text()},
				 {"request_file", update.requestFile()},
				 {"installer_file", update.installerFile()}};
	}

	{
		const UninstallPlan plan = app_.uninstallPlan(false);
		out["uninstall"] = {{"kind", plan.kind == UninstallPlan::Kind::Installer ? "installer" : "by_hand"},
				    {"program", plan.program},
				    {"arguments", plan.arguments},
				    {"paths", plan.paths},
				    {"pending", app_.uninstallRequest().pending()},
				    {"request_file", app_.uninstallRequest().requestFile()}};
	}

	out["encoders"] = {{"video_live", app_.outputs().encoderPool().liveVideoEncoders()},
			   {"audio_live", app_.outputs().encoderPool().liveAudioEncoders()}};

	video_t *video = obs_get_video();
	obs_video_info videoInfo{};
	obs_get_video_info(&videoInfo);
	out["obs"] = {
		{"version", obs_get_version_string()},
		{"video_active", obs_video_active()},
		{"base_width", videoInfo.base_width},
		{"base_height", videoInfo.base_height},
		{"output_width", videoInfo.output_width},
		{"output_height", videoInfo.output_height},
		{"render_total_frames", obs_get_total_frames()},
		{"render_lagged_frames", obs_get_lagged_frames()},
		{"encode_total_frames", video ? video_output_get_total_frames(video) : 0},
		{"encode_skipped_frames", video ? video_output_get_skipped_frames(video) : 0},
		{"active_fps", obs_get_active_fps()},
		{"average_frame_time_ms", obs_get_average_frame_time_ns() / 1000000.0},
		{"memory_mb", os_get_proc_resident_size() / (1024.0 * 1024.0)},
		{"private_mb", privateMemoryMb()},
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
	if (!env("RELAYDOCK_TEST_CREDENTIAL_PREFIX").empty() && !keepCredentials_)
		app_.vault().removeAll();

	// Close OBS: with the button the scenario named, or by closing the OBS window.
	bool closing = false;
	if (!quitWithButton_.empty()) {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (closing || !widget->isVisible())
				continue;
			for (QAbstractButton *button : widget->findChildren<QAbstractButton *>()) {
				if (!button->isVisible() || button->text().remove(QLatin1Char('&')).toStdString() != quitWithButton_)
					continue;
				logInfo("Scenario: closing OBS with the button \"{}\".", quitWithButton_);
				QMetaObject::invokeMethod(button, "click", Qt::QueuedConnection);
				closing = true;
				break;
			}
		}
		if (!closing)
			logError("Scenario: found no button \"{}\" to close OBS with.", quitWithButton_);
	}
	if (!closing) {
		if (auto *window = static_cast<QMainWindow *>(obs_frontend_get_main_window()))
			QMetaObject::invokeMethod(window, "close", Qt::QueuedConnection);
	}

	// The scenario asked for a question to be answered on the way out. The question runs its
	// own event loop inside the close, so a timer is the way to reach it.
	if (!quitConfirmButton_.empty()) {
		auto *clicker = new QTimer(this);
		clicker->setInterval(200);
		connect(clicker, &QTimer::timeout, this, [this, clicker] {
			for (QWidget *widget : QApplication::topLevelWidgets()) {
				auto *box = qobject_cast<QMessageBox *>(widget);
				if (!box || !box->isVisible())
					continue;
				for (QAbstractButton *button : box->buttons()) {
					if (button->text().remove(QLatin1Char('&')).toStdString() == quitConfirmButton_) {
						clicker->stop();
						logInfo("Scenario: pressing \"{}\" in \"{}\".", quitConfirmButton_,
							box->windowTitle().toStdString());
						button->click();
						return;
					}
				}
			}
		});
		clicker->start();
	}
}

} // namespace rd
