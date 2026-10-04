// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "ui/ui_common.h"

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class QCheckBox;
class QFormLayout;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;

namespace rd {

class UpdateChecker;

// The RelayDock settings window: a list of pages on the left, the chosen page on the right.
//
// Every change applies and saves at once. There is no OK or Apply step, so what you see is
// what RelayDock uses. Actions that cannot be undone ask first.
//
// Thread ownership: OBS UI thread.
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(UiHost &host, QWidget *parent = nullptr);

	// Page ids: general, platforms, streaming, video, audio, encoder, vertical, optimization,
	// performance, network, appearance, layout, security, diagnostics, updates, advanced, about.
	void showPage(const std::string &id);

	static const std::vector<std::string> &pageIds();

private:
	// One settings page under construction: a heading, an optional introduction, and a body
	// layout to fill.
	struct PageBuilder {
		QWidget *page = nullptr;
		QVBoxLayout *body = nullptr;
	};

	PageBuilder beginPage(const QString &title, const QString &intro);
	void addPage(const char *id, const QString &title, const char *icon, QWidget *page, std::function<void()> refresh);
	void refreshCurrent();
	// Saves the settings and tells the rest of RelayDock. Ignored while a page refreshes.
	void commit();
	QLabel *addNote(QVBoxLayout *layout, const QString &text);
	QLabel *addHeading(QVBoxLayout *layout, const QString &text);

	void buildGeneral();
	void buildPlatforms();
	void buildStreaming();
	void buildVideo();
	void buildAudio();
	void buildEncoder();
	void buildVertical();
	void buildOptimization();
	void buildPerformance();
	void buildNetwork();
	void buildAppearance();
	void buildLayout();
	void buildSecurity();
	void buildDiagnostics();
	void buildUpdates();
	void buildAdvanced();
	void buildAbout();

	struct Page {
		std::string id;
		QWidget *widget = nullptr;
		std::function<void()> refresh;
	};

	UiHost &host_;
	QListWidget *nav_;
	QStackedWidget *stack_;
	std::vector<Page> pages_;
	bool refreshing_ = false;
	UpdateChecker *updateChecker_ = nullptr;
};

} // namespace rd
