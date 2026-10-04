// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "ui/ui_common.h"

#include <QDialog>

class QLabel;
class QPushButton;

namespace rd {

// Signs in to Twitch for reading chat. RelayDock shows a code, the user confirms it on
// twitch.tv in a browser, and Twitch tells RelayDock that it may read chat. The Twitch password
// is typed on twitch.tv and nowhere else.
//
// The dialog starts the sign-in when it opens and ends it when it closes.
//
// Thread ownership: OBS UI thread.
class ChatSignInDialog : public QDialog {
	Q_OBJECT

public:
	explicit ChatSignInDialog(UiHost &host, QWidget *parent = nullptr);
	~ChatSignInDialog() override;

private:
	void begin();
	void showCode(const QString &code, const QString &page);
	void showResult(bool ok, const QString &login, const UserMessage &problem);

	UiHost &host_;
	QString page_;
	QLabel *code_;
	QLabel *state_;
	Banner *result_;
	QPushButton *open_;
	QPushButton *copy_;
	QPushButton *retry_;
	QPushButton *close_;
	bool done_ = false;
};

} // namespace rd
