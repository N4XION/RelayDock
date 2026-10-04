// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/exit_guard.h"

#include "app/app_context.h"
#include "outputs/output_manager.h"
#include "ui/ui_common.h"
#include "utils/log.h"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

namespace rd {

namespace {

// OBS repeats a close it had to put off, once per second. A confirmed close must not ask again.
constexpr int64_t kConfirmedForMs = 10000;

} // namespace

ExitGuard::ExitGuard(AppContext &app, QWidget *mainWindow, QObject *parent)
	: QObject(parent),
	  app_(app),
	  window_(mainWindow)
{
	if (window_)
		window_->installEventFilter(this);
}

bool ExitGuard::shouldAsk() const
{
	if (app_.isShutDown() || app_.outputs().activeCount() == 0)
		return false;

	// "Show active outputs warning on exit" in the OBS settings. Off means off for RelayDock too.
	config_t *user = obs_frontend_get_user_config();
	if (user && !config_get_bool(user, "General", "ConfirmOnExit"))
		return false;

	// With one of its own outputs running, OBS asks by itself. One question is enough.
	if (obs_frontend_streaming_active() || obs_frontend_recording_active() || obs_frontend_replay_buffer_active() ||
	    obs_frontend_virtualcam_active())
		return false;

	return confirmedAtMs_ < 0 || app_.clock().nowMs() - confirmedAtMs_ >= kConfirmedForMs;
}

bool ExitGuard::eventFilter(QObject *watched, QEvent *event)
{
	if (watched != window_ || event->type() != QEvent::Close)
		return QObject::eventFilter(watched, event);

	// A second close request while the question is open: the question is still the answer.
	if (asking_) {
		event->ignore();
		return true;
	}
	if (!shouldAsk())
		return false;

	const int active = app_.outputs().activeCount();
	QMessageBox box(QMessageBox::Warning, uiText("Exit.Title", "RelayDock is streaming"),
			uiTextN(active, "Exit.Text.One", "{0} destination is active. Closing OBS ends its stream.", "Exit.Text.Many",
				"{0} destinations are active. Closing OBS ends their streams.", active),
			QMessageBox::NoButton, window_);
	QPushButton *close = box.addButton(uiText("Exit.Close", "Close OBS"), QMessageBox::AcceptRole);
	QPushButton *stay = box.addButton(uiText("Exit.Stay", "Keep streaming"), QMessageBox::RejectRole);
	box.setDefaultButton(stay);
	box.setEscapeButton(stay);

	asking_ = true;
	box.exec();
	asking_ = false;

	if (box.clickedButton() == close) {
		logInfo("OBS is closing with {} active destination(s). You confirmed it.", active);
		confirmedAtMs_ = app_.clock().nowMs();
		return false; // OBS goes on with its own close handling
	}
	event->ignore();
	return true;
}

} // namespace rd
