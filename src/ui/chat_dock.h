// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "chat/chat_types.h"
#include "ui/ui_common.h"

#include <QWidget>

#include <vector>

class QTextBrowser;
class QPushButton;

namespace rd {

class ChatHub;

// One line that says what a platform's chat reader is doing: "Reading the chat of kai".
QString chatStatusText(ChatPlatform platform, const ChatStatus &status);
// The tone for that line: "ok", "warning", "error" or "neutral".
const char *chatStatusTone(const ChatStatus &status);

// The RelayDock Chat dock: the comments of every platform in one list, in the order they
// arrived, each marked with its platform. Gifts, paid messages, new subscribers and raids
// stand out.
//
// Chat is text that strangers wrote. Every character of it is shown as text: nothing in a
// comment can become a link, a picture or formatting.
//
// Thread ownership: OBS UI thread.
class ChatDockWidget : public QWidget {
	Q_OBJECT

public:
	explicit ChatDockWidget(UiHost &host, QWidget *parent = nullptr);

private:
	struct PlatformRow {
		ChatPlatform platform = ChatPlatform::Twitch;
		ProviderBadge *badge = nullptr;
		ElidedLabel *text = nullptr;
	};

	void refreshStatus();
	void refreshEmptyState();
	void append(const std::vector<ChatEvent> &events);
	// Draws every line again, after the list was cleared or the colours changed.
	void rebuild();
	void registerBadges();
	QString lineHtml(const ChatEvent &event) const;

	UiHost &host_;
	ChatHub &hub_;
	std::vector<PlatformRow> rows_;
	Banner *problem_;
	QTextBrowser *view_;
	QLabel *empty_;
	QPushButton *setUp_;
	bool showTime_ = true;
};

} // namespace rd
