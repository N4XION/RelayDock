// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "performance/effective_settings.h"
#include "ui/ui_common.h"

#include <QFrame>
#include <QPoint>

#include <string>

class QCheckBox;
class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QToolButton;
class QVBoxLayout;

namespace rd {

class ConnectionTester;

// One destination in the dock: what it is, what state it is in, and the controls to start,
// stop and manage it.
//
// A card never changes another destination. Everything it does goes through AppContext and
// OutputManager with its own destination id.
//
// Thread ownership: OBS UI thread.
class DestinationCard : public QFrame {
	Q_OBJECT

public:
	static constexpr const char *kDragMimeType = "application/x-relaydock-destination";

	DestinationCard(UiHost &host, std::string id, QWidget *parent = nullptr);

	const std::string &id() const { return id_; }

	void setExpanded(bool expanded);

	// Rereads everything. `planned` is what a start would use, for a destination that has no
	// output yet. May be nullptr.
	void refresh(const EffectiveDestination *planned);
	// Updates only the numbers of a live destination. Cheap, runs every second.
	void refreshStats();

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	void buildMenu();
	void toggleStartStop();
	void startTestConnection();
	void removeAfterConfirm();
	void move(int delta);
	QString statsLine() const;
	// Lets the status label breathe while a connection attempt runs. Off when animations are.
	void setPulsing(bool on);

	UiHost &host_;
	std::string id_;
	bool expanded_ = true;
	EffectiveDestination planned_;
	bool hasPlanned_ = false;

	QLabel *grip_;
	QCheckBox *enabled_;
	ProviderBadge *badge_;
	ElidedLabel *name_;
	ElidedLabel *summary_;
	Pill *pill_;
	QToolButton *startStop_;
	QToolButton *menuButton_;

	QWidget *details_;
	ElidedLabel *serverLine_;
	ElidedLabel *encoderLine_;
	ElidedLabel *statusLine_;
	Banner *problem_;
	Banner *testResult_;

	ConnectionTester *tester_ = nullptr;
	QGraphicsOpacityEffect *pillEffect_ = nullptr;
	QPropertyAnimation *pulse_ = nullptr;
	QPoint dragStart_;
	bool dragArmed_ = false;
	bool showsStop_ = false;
};

} // namespace rd
