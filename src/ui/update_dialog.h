// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "ui/ui_common.h"
#include "update/update_check.h"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace rd {

// Tells the user that a newer RelayDock exists, and gets it for them.
//
// For a RelayDock that the installer put on this PC, the window offers Update now: RelayDock
// downloads the installer of the release, checks it against the checksums of the release and
// starts it. The installer waits until OBS has closed. app/update_install.h has the details.
// A plugin cannot replace its own file while OBS runs, which is why nothing is installed
// before that.
//
// A RelayDock that was copied by hand has no installer to hand over to. For that one the
// window opens the download in the browser and says what to do with it.
//
// Nothing starts without a click. "Skip this version" is remembered in the settings. "Later"
// asks again at the next start.
class UpdateDialog : public QDialog {
	Q_OBJECT

public:
	UpdateDialog(UiHost &host, const ReleaseInfo &release, QWidget *parent = nullptr);

private:
	// What the update of this release is doing right now, as an UpdateInstall::State.
	int stage() const;
	void refresh();
	void showProgress();
	void onPrimary();

	UiHost &host_;
	ReleaseInfo release_;
	bool inPlace_ = false; // Update now can serve this release with this RelayDock

	QWidget *steps_ = nullptr;        // What happens, before anything started
	QWidget *progress_ = nullptr;     // The download
	QProgressBar *bar_ = nullptr;
	QLabel *progressText_ = nullptr;
	Banner *state_ = nullptr;         // Ready, or what went wrong
	QPushButton *skip_ = nullptr;
	QPushButton *notes_ = nullptr;
	QPushButton *browser_ = nullptr;  // The download in the browser, after Update now failed
	QPushButton *later_ = nullptr;
	QPushButton *cancel_ = nullptr;
	QPushButton *primary_ = nullptr;
};

} // namespace rd
