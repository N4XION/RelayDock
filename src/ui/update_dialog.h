// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "ui/ui_common.h"
#include "update/update_check.h"

#include <QDialog>

namespace rd {

// Tells the user that a newer RelayDock exists and how to get it.
//
// RelayDock does not update itself. A plugin cannot replace its own file while OBS runs, and a
// program that downloads and starts other programs is what security software looks for. So
// the window opens the installer's download in the browser and says what to do with it:
// close OBS, run it, and everything stays.
//
// "Skip this version" is remembered in the settings. "Later" asks again at the next start.
class UpdateDialog : public QDialog {
	Q_OBJECT

public:
	UpdateDialog(UiHost &host, const ReleaseInfo &release, QWidget *parent = nullptr);

private:
	UiHost &host_;
	ReleaseInfo release_;
};

} // namespace rd
