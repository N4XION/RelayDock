// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "ui/key_clipboard.h"
#include "ui/ui_common.h"

#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace rd {

class AppContext;
class DestinationCard;
class SettingsDialog;
class UpdateChecker;

// Holds the destination cards and lays them out as a list or a grid. Accepts a dragged card
// and reports where it was dropped.
class CardList : public QWidget {
	Q_OBJECT

public:
	explicit CardList(QWidget *parent = nullptr);

	void setCards(const std::vector<DestinationCard *> &cards);
	void setGrid(bool grid);
	const std::vector<DestinationCard *> &cards() const { return cards_; }

	// Called with the dragged destination's id and the index it should end up at.
	std::function<void(const std::string &id, int index)> onReorder;

protected:
	void resizeEvent(QResizeEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	int columnsForWidth(int width) const;
	void relayout();

	QGridLayout *grid_;
	std::vector<DestinationCard *> cards_;
	bool gridMode_ = false;
	int columns_ = 1;
};

// The RelayDock dock: toolbar, destination cards, suggestions, performance and network.
//
// Cost while idle: no timer of its own. It redraws when OutputManager or PerformanceMonitor
// report something new, and those tick once per second at most.
//
// Thread ownership: OBS UI thread.
class RelayDockWidget : public QWidget, public UiHost {
	Q_OBJECT

public:
	explicit RelayDockWidget(AppContext &app, QWidget *parent = nullptr);
	~RelayDockWidget() override;

	// OBS finished loading: the encoder list is complete, so cards can show real settings.
	void onObsFinishedLoading();
	// The OBS theme changed.
	void onObsThemeChanged();

	// ---- UiHost -------------------------------------------------------------------------------
	AppContext &app() override { return app_; }
	Theme &theme() override { return *theme_; }
	KeyClipboard &clipboard() override { return clipboard_; }
	QWidget *dialogParent() override;
	void editDestination(const std::string &id) override;
	void addDestination(const std::string &providerId) override;
	void openSettings(const std::string &pageId) override;
	void openVerticalEditor(const std::string &layoutId) override;
	void showLegal(const std::string &documentId) override;
	void runPreflight(bool startAfterwards) override;
	void showUpdate(const ReleaseInfo &release) override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	void buildToolbar();
	void buildSections();
	void applyLayout();
	void syncCards();
	void refreshCards();
	void refreshToolbar();
	void refreshSuggestions();
	void refreshPerformance();
	void refreshNetwork();
	void refreshLegalGate();
	void startAllEnabled();
	void stopAll();
	void shutdownUi();
	QPixmap providerIcon(const std::string &providerId);

	AppContext &app_;
	Theme *theme_;
	KeyClipboard clipboard_;
	bool closed_ = false;

	QVBoxLayout *rootLayout_;
	QWidget *toolbar_;
	QPushButton *addButton_;
	QPushButton *startAllButton_;
	QPushButton *stopAllButton_;
	QToolButton *preflightButton_;
	QToolButton *viewButton_;
	QToolButton *gridButton_;
	QToolButton *settingsButton_;
	QLabel *summary_;

	QScrollArea *scroll_;
	QWidget *content_;
	QVBoxLayout *contentLayout_;

	Banner *updateBanner_;
	UpdateChecker *startupCheck_ = nullptr;
	ReleaseInfo offeredRelease_; // The newer version the banner and the update window are about
	QFrame *legalPanel_;
	QLabel *legalText_;

	std::map<std::string, QWidget *> sections_;
	CardList *cardList_;
	QLabel *emptyLabel_;
	QVBoxLayout *suggestionsLayout_;
	QLabel *performanceValues_[8] = {};
	QComboBox *modeCombo_;
	QCheckBox *preventLag_;
	QLabel *networkLine_;
	QProgressBar *networkMeter_;
	QLabel *networkDetail_;
	QPushButton *networkSetup_;

	QPointer<SettingsDialog> settings_;
	QPixmap background_;
	QString backgroundPath_;
};

} // namespace rd
