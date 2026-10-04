// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <QObject>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <map>
#include <string>

struct os_cpu_usage_info;

namespace rd {

class AppContext;

// Drives RelayDock from a script, for integration tests, performance runs and documentation
// screenshots. It calls the same functions the interface calls, inside a real OBS.
//
// This file is compiled only when the build sets RELAYDOCK_TEST_HOOKS=ON. Release builds do
// not contain it, so an installed RelayDock cannot be scripted through an environment variable.
//
// The scenario is a JSON file named by the RELAYDOCK_SCENARIO environment variable. Results go
// to the file named by RELAYDOCK_SCENARIO_RESULT, rewritten after every step so a crash still
// leaves the steps that ran. tests/integration/README.md lists the steps.
//
// Thread ownership: OBS UI thread.
class ScenarioRunner : public QObject {
	Q_OBJECT

public:
	explicit ScenarioRunner(AppContext &app, QObject *parent = nullptr);
	~ScenarioRunner() override;

	// Loads the scenario from the environment and starts it. Returns false when no
	// scenario is set or the file cannot be read.
	bool startFromEnvironment();

private:
	enum class StepResult { Done, Waiting, Failed };

	void tick();
	StepResult beginStep(const nlohmann::json &step, std::string &detail);
	StepResult pollStep(const nlohmann::json &step, std::string &detail);
	// Interface steps, in scenario_ui.cpp. Returns false when `op` is not one of them.
	bool uiStep(const std::string &op, const nlohmann::json &step, StepResult &result, std::string &detail);
	nlohmann::json snapshot();
	void recordStep(const nlohmann::json &step, bool ok, const std::string &detail);
	void writeResults();
	void finish(bool ok, const std::string &failure);

	std::string idFor(const nlohmann::json &step, std::string &detail) const;

	AppContext &app_;
	QTimer timer_;
	nlohmann::json steps_ = nlohmann::json::array();
	nlohmann::json results_;
	std::map<std::string, std::string> refs_; // scenario name -> destination id
	std::string resultPath_;
	size_t index_ = 0;
	bool waiting_ = false;
	bool finished_ = false;
	bool keepCredentials_ = false;
	int64_t stepStartedMs_ = 0;
	int64_t scenarioStartedMs_ = 0;
	int64_t lastTickMs_ = 0;
	int64_t maxTickGapMs_ = 0;
	os_cpu_usage_info *cpu_ = nullptr;
};

} // namespace rd
