// SPDX-License-Identifier: GPL-2.0-or-later
/*
RelayDock - multistream plugin for OBS Studio
Copyright (C) 2026 RelayDock contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include "app/app_context.h"
#include "build_info.h"
#include "utils/i18n.h"
#include "utils/log.h"

#ifdef RELAYDOCK_TEST_HOOKS
#include "testing/scenario_runner.h"
#endif

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("relaydock", "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "RelayDock";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}

namespace {

constexpr const char *kDockId = "relaydock";

// Module state. Created in obs_module_load, shut down on the frontend's exit event while
// libobs is still fully alive, destroyed in obs_module_unload.
std::unique_ptr<rd::AppContext> g_app;
#ifdef RELAYDOCK_TEST_HOOKS
std::unique_ptr<rd::ScenarioRunner> g_scenario;
#endif

void obsLogSink(rd::LogLevel level, const std::string &line)
{
	int obsLevel = LOG_INFO;
	switch (level) {
	case rd::LogLevel::Debug:
		obsLevel = LOG_DEBUG;
		break;
	case rd::LogLevel::Info:
		obsLevel = LOG_INFO;
		break;
	case rd::LogLevel::Warning:
		obsLevel = LOG_WARNING;
		break;
	case rd::LogLevel::Error:
		obsLevel = LOG_ERROR;
		break;
	}
	// "%s" keeps a percent sign inside the message from being read as a format.
	blog(obsLevel, "[RelayDock] %s", line.c_str());
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (g_app) {
			g_app->onObsFinishedLoading();
#ifdef RELAYDOCK_TEST_HOOKS
			g_scenario = std::make_unique<rd::ScenarioRunner>(*g_app);
			if (!g_scenario->startFromEnvironment())
				g_scenario.reset();
#endif
		}
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		// OBS is closing. Stop every stream and release every OBS object now, while
		// libobs and the frontend are still intact.
#ifdef RELAYDOCK_TEST_HOOKS
		g_scenario.reset();
#endif
		if (g_app)
			g_app->shutdown();
		break;
	default:
		break;
	}
}

} // namespace

bool obs_module_load(void)
{
	rd::setLogSink(obsLogSink);

	const rd::BuildInfo &info = rd::buildInfo();
	rd::logInfo("Loading version {} (build {}, {}, {})", rd::buildVersionString(), info.buildNumber,
		    info.buildDate, info.architecture);
	rd::logInfo("Running in OBS Studio {}. Built for OBS Studio {} and newer.", obs_get_version_string(),
		    info.obsMinimumVersion);
#ifdef RELAYDOCK_TEST_HOOKS
	rd::logWarning("This build contains the test scenario runner. Do not use it for real streams.");
#endif

	g_app = std::make_unique<rd::AppContext>();
	g_app->initialize();

	// OBS owns the dock and deletes the widget when it shuts down.
	auto *root = new QWidget();
	auto *layout = new QVBoxLayout(root);
	auto *label = new QLabel(QString::fromUtf8("%1 %2").arg(QString::fromUtf8(info.displayName),
								 QString::fromUtf8(info.version)),
				 root);
	layout->addWidget(label);
	layout->addStretch(1);

	if (!obs_frontend_add_dock_by_id(kDockId, info.displayName, root)) {
		rd::logError("OBS refused to add the RelayDock dock. Another dock already uses the id '{}'.", kDockId);
		delete root;
		g_app.reset();
		return false;
	}

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	rd::logInfo("Loaded. Open the dock from the OBS Docks menu.");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
#ifdef RELAYDOCK_TEST_HOOKS
	g_scenario.reset();
#endif
	g_app.reset();

	rd::logInfo("Unloaded.");
	rd::setTranslator(nullptr);
	rd::setLogSink(nullptr);
}
