// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// The Chat page of the settings window. It is a member of SettingsDialog like every other page
// and lives in a file of its own because of its size.
#include "ui/settings_dialog.h"

#include "app/app_context.h"
#include "app/chat_hub.h"
#include "build_info.h"
#include "chat/twitch_protocol.h"
#include "chat/youtube_protocol.h"
#include "legal/legal_documents.h"
#include "ui/chat_dock.h"
#include "ui/chat_signin_dialog.h"
#include "utils/strings.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace rd {

void SettingsDialog::buildChat()
{
	PageBuilder p = beginPage(uiText("Settings.Chat", "Chat"),
				  uiText("Chat.Intro",
					 "The RelayDock Chat dock shows the comments of your Twitch and YouTube streams in one list, with gifts, paid messages and new subscribers. RelayDock reads them through the interfaces these platforms publish. TikTok and Facebook publish none that RelayDock can use, so their comments are not in the list."));
	AppContext &app = host_.app();
	ChatHub &hub = app.chat();

	// The review of the legal documents comes before RelayDock talks to a platform.
	const auto reviewed = [this, &app] {
		if (legalComplete(app.config().legal))
			return true;
		host_.showLegal({});
		return false;
	};

	// ---- Twitch -----------------------------------------------------------------------------
	addHeading(p.body, uiText("Chat.Twitch", "Twitch"));
	QLabel *twitchState = makeLabel(QString(), nullptr, p.page);
	twitchState->setWordWrap(true);
	p.body->addWidget(twitchState);
	QLabel *twitchProblem = addNote(p.body, QString());

	auto *twitchButtons = new QHBoxLayout();
	auto *signIn = new QPushButton(uiText("Chat.Twitch.SignIn", "Sign in with Twitch..."), p.page);
	auto *signOut = new QPushButton(uiText("Chat.Twitch.SignOut", "Sign out"), p.page);
	twitchButtons->addWidget(signIn);
	twitchButtons->addWidget(signOut);
	twitchButtons->addStretch(1);
	p.body->addLayout(twitchButtons);
	auto *readTwitch = new QCheckBox(uiText("Chat.Twitch.Read", "Read my Twitch chat"), p.page);
	p.body->addWidget(readTwitch);
	addNote(p.body, uiText("Chat.Twitch.Note",
			       "You sign in on twitch.tv in your browser, with a code RelayDock shows you. RelayDock gets one permission, to read chat, and keeps the sign-in in Windows Credential Manager. Sign out forgets it on this PC."));

	auto *clientRow = new QHBoxLayout();
	auto *clientId = new QLineEdit(p.page);
	clientId->setAccessibleName(uiText("Chat.Twitch.ClientId", "Twitch application id"));
	clientId->setMaxLength(64);
	const bool buildHasId = twitch::validClientId(buildInfo().twitchClientId);
	clientId->setPlaceholderText(buildHasId ? uiText("Chat.Twitch.ClientId.Built", "The one RelayDock comes with")
						: uiText("Chat.Twitch.ClientId.None", "None yet"));
	clientRow->addWidget(makeLabel(uiText("Chat.Twitch.ClientId", "Twitch application id"), nullptr, p.page));
	clientRow->addWidget(clientId, 1);
	p.body->addLayout(clientRow);
	addNote(p.body, buildHasId ? uiText("Chat.Twitch.ClientId.Note",
					    "Leave this empty. It is only for an application you registered with Twitch yourself.")
				   : uiText("Chat.Twitch.ClientId.NoteNone",
					    "This RelayDock comes without an application id. Register one with Twitch, free of charge, and enter its Client ID here. The chat guide shows how. A Client ID is a public name, not a secret."));

	// ---- YouTube ----------------------------------------------------------------------------
	addHeading(p.body, uiText("Chat.YouTube", "YouTube"));
	QLabel *youtubeState = makeLabel(QString(), nullptr, p.page);
	youtubeState->setWordWrap(true);
	p.body->addWidget(youtubeState);
	QLabel *youtubeProblem = addNote(p.body, QString());

	auto *keyRow = new QHBoxLayout();
	QLabel *keySaved = makeLabel(uiText("Chat.YouTube.KeySaved", "An API key is saved."), nullptr, p.page);
	auto *keyEntry = new QLineEdit(p.page);
	keyEntry->setAccessibleName(uiText("Chat.YouTube.Key", "YouTube API key"));
	keyEntry->setEchoMode(QLineEdit::Password);
	keyEntry->setMaxLength(120);
	keyEntry->setPlaceholderText(uiText("Chat.YouTube.Key.Placeholder", "Paste your API key"));
	auto *saveKey = new QPushButton(uiText("Chat.YouTube.Key.Save", "Save key"), p.page);
	auto *replaceKey = new QPushButton(uiText("Chat.YouTube.Key.Replace", "Replace..."), p.page);
	auto *removeKey = new QPushButton(uiText("Chat.YouTube.Key.Remove", "Remove"), p.page);
	keyRow->addWidget(makeLabel(uiText("Chat.YouTube.Key", "YouTube API key"), nullptr, p.page));
	keyRow->addWidget(keySaved);
	keyRow->addWidget(keyEntry, 1);
	keyRow->addWidget(saveKey);
	keyRow->addWidget(replaceKey);
	keyRow->addWidget(removeKey);
	keyRow->addStretch(0);
	p.body->addLayout(keyRow);
	QLabel *keyNote = addNote(p.body, QString());
	addNote(p.body, uiText("Chat.YouTube.Key.Note",
			       "YouTube's rules do not allow RelayDock to come with a key, so you create one of your own, free of charge. The chat guide shows how. RelayDock keeps the key in Windows Credential Manager and never shows it again."));

	auto *videoRow = new QHBoxLayout();
	auto *video = new QLineEdit(p.page);
	video->setAccessibleName(uiText("Chat.YouTube.Video", "Link to your stream"));
	video->setMaxLength(300);
	video->setPlaceholderText(uiText("Chat.YouTube.Video.Placeholder", "https://www.youtube.com/watch?v=..."));
	auto *connectButton = new QPushButton(uiText("Chat.YouTube.Connect", "Connect"), p.page);
	auto *disconnectButton = new QPushButton(uiText("Chat.YouTube.Disconnect", "Disconnect"), p.page);
	videoRow->addWidget(makeLabel(uiText("Chat.YouTube.Video", "Link to your stream"), nullptr, p.page));
	videoRow->addWidget(video, 1);
	videoRow->addWidget(connectButton);
	videoRow->addWidget(disconnectButton);
	p.body->addLayout(videoRow);
	addNote(p.body, uiText("Chat.YouTube.Video.Note",
			       "A YouTube stream has a new link every time. Paste the link of the stream that is live, from your browser or from YouTube Studio."));

	auto *pollRow = new QHBoxLayout();
	auto *poll = new QSpinBox(p.page);
	poll->setAccessibleName(uiText("Chat.YouTube.Poll", "Pause between requests"));
	poll->setRange(kChatPollSecondsMin, kChatPollSecondsMax);
	poll->setSuffix(uiText("Chat.YouTube.Poll.Unit", " s"));
	pollRow->addWidget(makeLabel(uiText("Chat.YouTube.Poll", "Pause between requests"), nullptr, p.page));
	pollRow->addWidget(poll);
	pollRow->addStretch(1);
	p.body->addLayout(pollRow);
	QLabel *quota = addNote(p.body, QString());

	// ---- Display ------------------------------------------------------------------------------
	addHeading(p.body, uiText("Chat.Display", "Display"));
	auto *showTime = new QCheckBox(uiText("Chat.ShowTime", "Show the time in front of each line"), p.page);
	p.body->addWidget(showTime);

	const std::string project = repositoryUrl();
	if (!project.empty()) {
		auto *guide = new QPushButton(uiText("Chat.Guide", "Open the chat guide"), p.page);
		p.body->addWidget(guide, 0, Qt::AlignLeft);
		const QString url = qs(project + "/blob/main/docs/chat.md");
		connect(guide, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
	}
	p.body->addStretch(1);

	// Whether the user asked to type a new key over a saved one.
	auto replacing = std::make_shared<bool>(false);

	const auto refresh = [=, this, &app, &hub] {
		const ChatConfig &chat = app.config().chat;

		// Twitch
		const ChatStatus twitch = hub.status(ChatPlatform::Twitch);
		const bool signedIn = hub.twitchSignedIn();
		const bool hasId = twitch::validClientId(hub.twitchClientId());
		QString line = chatStatusText(ChatPlatform::Twitch, twitch);
		if (signedIn && twitch.state != ChatState::Connected && !chat.twitchLogin.empty())
			line += QStringLiteral(" ") + uiTextF("Chat.Twitch.SignedInAs", "Signed in as {0}.", chat.twitchLogin);
		twitchState->setText(line);
		setStyleProperty(twitchState, "rdTone", QString::fromUtf8(chatStatusTone(twitch)));
		twitchProblem->setText(qs(twitch.message.text()));
		twitchProblem->setVisible(!twitch.message.empty());
		signIn->setVisible(!signedIn);
		signIn->setEnabled(hasId);
		signOut->setVisible(signedIn);
		readTwitch->setVisible(signedIn);
		readTwitch->setChecked(chat.twitchEnabled);
		if (!clientId->hasFocus())
			clientId->setText(qs(chat.twitchClientId));
		// Another application id means another sign-in. Sign out first.
		clientId->setEnabled(!signedIn);

		// YouTube
		const ChatStatus youtube = hub.status(ChatPlatform::YouTube);
		const bool keyIsSaved = hub.youtubeKeySaved();
		const bool typing = !keyIsSaved || *replacing;
		const bool reading = youtube.state == ChatState::Connecting || youtube.state == ChatState::Connected ||
				     youtube.state == ChatState::Waiting;
		youtubeState->setText(chatStatusText(ChatPlatform::YouTube, youtube));
		setStyleProperty(youtubeState, "rdTone", QString::fromUtf8(chatStatusTone(youtube)));
		youtubeProblem->setText(qs(youtube.message.text()));
		youtubeProblem->setVisible(!youtube.message.empty());
		keySaved->setVisible(!typing);
		keyEntry->setVisible(typing);
		saveKey->setVisible(typing);
		replaceKey->setVisible(!typing);
		removeKey->setVisible(keyIsSaved);
		if (!video->hasFocus() && video->text().isEmpty())
			video->setText(qs(chat.youtubeVideo));
		connectButton->setVisible(!reading);
		connectButton->setEnabled(keyIsSaved);
		disconnectButton->setVisible(reading);
		if (!poll->hasFocus())
			poll->setValue(chat.youtubePollSeconds);
		quota->setText(uiTextF("Chat.YouTube.Quota.Note",
				       "Every request uses a little of the daily amount YouTube gives your key. A longer pause makes it last longer. Requests sent since OBS started: {0}.",
				       hub.youtubeRequests()));

		showTime->setChecked(chat.showTime);
	};

	// ---- Twitch actions -----------------------------------------------------------------------
	connect(signIn, &QPushButton::clicked, this, [=, this] {
		if (!reviewed())
			return;
		ChatSignInDialog dialog(host_, this);
		dialog.exec();
		refreshCurrent();
	});
	connect(signOut, &QPushButton::clicked, this, [=, this, &hub] {
		QMessageBox box(QMessageBox::Question, uiText("Chat.Twitch.SignOut.Title", "Sign out of Twitch"),
				uiText("Chat.Twitch.SignOut.Question", "Forget the Twitch sign-in on this PC?"),
				QMessageBox::Yes | QMessageBox::Cancel, this);
		box.setInformativeText(uiText("Chat.Twitch.SignOut.Detail",
					      "RelayDock stops reading your Twitch chat. To end RelayDock's permission on Twitch as well, open twitch.tv, Settings, Connections."));
		box.setDefaultButton(QMessageBox::Cancel);
		if (box.exec() != QMessageBox::Yes)
			return;
		hub.signOutTwitch();
		refreshCurrent();
	});
	connect(readTwitch, &QCheckBox::toggled, this, [this, &hub](bool on) {
		if (refreshing_)
			return;
		hub.setTwitchEnabled(on);
	});
	connect(clientId, &QLineEdit::editingFinished, this, [=, this, &app, &hub] {
		const std::string typed = trim(ss(clientId->text()));
		if (typed == app.config().chat.twitchClientId)
			return;
		if (!typed.empty() && !twitch::validClientId(typed)) {
			twitchProblem->setText(uiText("Chat.Twitch.ClientId.Invalid", "A Twitch application id is made of letters and digits only."));
			twitchProblem->setVisible(true);
			return;
		}
		app.config().chat.twitchClientId = typed;
		commit();
		hub.apply();
	});

	// ---- YouTube actions ----------------------------------------------------------------------
	connect(saveKey, &QPushButton::clicked, this, [=, this, &hub] {
		if (!reviewed())
			return;
		const std::string typed = trim(ss(keyEntry->text()));
		keyEntry->clear();
		if (!youtube::plausibleApiKey(typed)) {
			keyNote->setText(uiText("Chat.YouTube.Key.Invalid",
						"This does not look like an API key. A key is one word of letters, digits, hyphens and underscores."));
			setStyleProperty(keyNote, "rdTone", QStringLiteral("warning"));
			return;
		}
		const SecretVault::SetResult saved = hub.setYouTubeKey(SecretString(typed));
		*replacing = false;
		if (saved.result.ok()) {
			keyNote->setText(uiText("Chat.YouTube.Key.Saved", "The key is saved in Windows Credential Manager."));
			setStyleProperty(keyNote, "rdTone", QStringLiteral("ok"));
		} else if (saved.keptForSessionOnly) {
			keyNote->setText(uiText("Chat.YouTube.Key.SessionOnly",
						"Windows did not save the key. RelayDock keeps it in memory until OBS closes."));
			setStyleProperty(keyNote, "rdTone", QStringLiteral("warning"));
		} else {
			keyNote->setText(uiText("Chat.YouTube.Key.NotSaved", "The key could not be saved."));
			setStyleProperty(keyNote, "rdTone", QStringLiteral("error"));
		}
		refreshCurrent();
	});
	connect(keyEntry, &QLineEdit::returnPressed, saveKey, &QPushButton::click);
	connect(replaceKey, &QPushButton::clicked, this, [=, this] {
		*replacing = true;
		keyNote->clear();
		refreshCurrent();
		keyEntry->setFocus();
	});
	connect(removeKey, &QPushButton::clicked, this, [=, this, &hub] {
		QMessageBox box(QMessageBox::Question, uiText("Chat.YouTube.Key.Remove.Title", "Remove the API key"),
				uiText("Chat.YouTube.Key.Remove.Question", "Remove the saved YouTube API key?"),
				QMessageBox::Yes | QMessageBox::Cancel, this);
		box.setInformativeText(uiText("Chat.YouTube.Key.Remove.Detail",
					      "RelayDock deletes it from Windows Credential Manager and stops reading YouTube chat. The key stays valid at Google until you delete it there."));
		box.setDefaultButton(QMessageBox::Cancel);
		if (box.exec() != QMessageBox::Yes)
			return;
		hub.removeYouTubeKey();
		*replacing = false;
		keyNote->clear();
		refreshCurrent();
	});
	connect(connectButton, &QPushButton::clicked, this, [=, this, &hub] {
		if (!reviewed())
			return;
		UserMessage problem;
		if (!hub.connectYouTube(ss(video->text()), problem)) {
			youtubeProblem->setText(qs(problem.text()));
			youtubeProblem->setVisible(true);
			setStyleProperty(youtubeProblem, "rdTone", QStringLiteral("warning"));
			return;
		}
		setStyleProperty(youtubeProblem, "rdTone", QString());
		refreshCurrent();
	});
	connect(video, &QLineEdit::returnPressed, connectButton, &QPushButton::click);
	connect(disconnectButton, &QPushButton::clicked, this, [this, &hub] {
		hub.disconnectYouTube();
		refreshCurrent();
	});
	connect(poll, &QSpinBox::valueChanged, this, [this, &app](int value) {
		if (refreshing_)
			return;
		// It takes effect with the next Connect.
		app.config().chat.youtubePollSeconds = value;
		commit();
	});
	connect(showTime, &QCheckBox::toggled, this, [this, &app](bool on) {
		if (refreshing_)
			return;
		app.config().chat.showTime = on;
		commit();
	});

	// A reader reports from its own pace. The page follows while it is the one shown.
	connect(&hub, &ChatHub::statusChanged, this, [this] {
		if (currentPageId() == "chat")
			refreshCurrent();
	});

	addPage("chat", uiText("Settings.Chat", "Chat"), "users", p.page, refresh);
}

} // namespace rd
