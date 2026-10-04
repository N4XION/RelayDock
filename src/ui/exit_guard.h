// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <QObject>

#include <cstdint>

class QWidget;

namespace rd {

class AppContext;

// Asks before OBS closes while a RelayDock destination is active.
//
// OBS asks that question for its own stream and recordings ("Show active outputs warning on
// exit" under Settings, Advanced). It knows nothing about RelayDock's outputs, so without this
// guard a stray click on the close button ends every stream without a word.
//
// The guard watches the OBS main window for its close event. It follows the OBS setting, and
// it stays silent when OBS is about to ask by itself.
//
// Thread ownership: OBS UI thread.
class ExitGuard : public QObject {
	Q_OBJECT

public:
	ExitGuard(AppContext &app, QWidget *mainWindow, QObject *parent);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	bool shouldAsk() const;

	AppContext &app_;
	QWidget *window_;
	bool asking_ = false;
	int64_t confirmedAtMs_ = -1;
};

} // namespace rd
