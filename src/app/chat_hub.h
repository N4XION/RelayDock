// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "chat/chat_types.h"
#include "chat/twitch_chat.h"
#include "chat/youtube_chat.h"
#include "security/secret_vault.h"

#include <QObject>
#include <QString>

#include <memory>
#include <string>
#include <vector>

namespace rd {

class AppContext;

// Live chat inside RelayDock: it owns the readers for Twitch and YouTube, the sign-in, and the
// one list that every platform's comments and events go into.
//
// The readers work on their own threads. Everything they report arrives here on the OBS UI
// thread, and the interface listens to the signals below.
//
// What is kept: the newest events in memory, until OBS closes. Nothing of the chat is written
// to disk, to a log or to a diagnostics report.
//
// Thread ownership: OBS UI thread.
class ChatHub : public QObject {
	Q_OBJECT

public:
	explicit ChatHub(AppContext &app, QObject *parent = nullptr);
	~ChatHub() override;

	// Starts and stops the readers so they match the settings and what is saved. Call it when
	// OBS has loaded and after anything about chat changed.
	void apply();
	// OBS is closing. Ends every reader and the sign-in.
	void shutdown();

	const ChatTimeline &timeline() const { return timeline_; }
	void clearTimeline();

	// What a platform's reader is doing, with words for the user when it is not reading.
	ChatStatus status(ChatPlatform platform) const;

	// ---- Twitch ---------------------------------------------------------------------------------
	// The application id RelayDock signs in with: the user's own when one is set, else the one
	// this build carries. Empty when there is neither.
	std::string twitchClientId() const;
	bool twitchSignedIn() const;
	// Shows a code through twitchSignInCode, waits for the user to confirm it on twitch.tv and
	// ends with twitchSignInFinished.
	void startTwitchSignIn();
	void cancelTwitchSignIn();
	bool twitchSignInRunning() const;
	// Forgets the sign-in on this PC and stops reading.
	void signOutTwitch();
	// Switches the reading of Twitch chat on or off. The sign-in stays.
	void setTwitchEnabled(bool enabled);

	// ---- YouTube --------------------------------------------------------------------------------
	bool youtubeKeySaved() const;
	SecretVault::SetResult setYouTubeKey(const SecretString &key);
	void removeYouTubeKey();
	// Starts reading the chat of the stream that `videoInput` names, a link or a video id.
	// Returns false with words for the user when it names none or no key is saved.
	bool connectYouTube(const std::string &videoInput, UserMessage &problem);
	void disconnectYouTube();
	// Requests sent to YouTube since OBS started. Each one costs quota of the user's key.
	unsigned long long youtubeRequests() const;

#ifdef RELAYDOCK_TEST_HOOKS
	// A test points RelayDock at stand-ins for the platforms on this PC and shortens the waits.
	struct TestSetup {
		twitch::Endpoints twitch;
		youtube::Endpoints youtube;
		long long retryFirstMs = 0; // 0 keeps the usual value
		long long retryMaxMs = 0;
		long long signInPollMs = 0;
		long long keepaliveGraceMs = 0;
		long long youtubeMinPollMs = 0;
		long long youtubeNotLiveRetryMs = 0;
	};
	void setTestSetup(const TestSetup &setup);
#endif

Q_SIGNALS:
	// New events were added to the end of the list.
	void eventsAdded(const std::vector<rd::ChatEvent> &events);
	void timelineCleared();
	// A reader's status changed, or something about the accounts did.
	void statusChanged();
	// The sign-in has a code. `page` is the page on twitch.tv to open.
	void twitchSignInCode(const QString &code, const QString &page);
	void twitchSignInFinished(bool ok, const QString &login, const rd::UserMessage &problem);

private:
	TwitchChatConfig twitchConfig() const;
	TwitchTokenStore twitchStore();
	ChatWorker::Callbacks callbacksFor(ChatPlatform platform);
	void onEvents(const std::vector<ChatEvent> &events);
	void onStatus(ChatPlatform platform, const ChatStatus &status);
	void startTwitch();
	void startYouTube();
	std::string userAgent() const;

	AppContext &app_;
	ChatTimeline timeline_{500};
	std::unique_ptr<TwitchChat> twitch_;
	std::unique_ptr<YouTubeChat> youtube_;
	std::unique_ptr<TwitchSignIn> signIn_;
	ChatStatus twitchStatus_;
	ChatStatus youtubeStatus_;
	unsigned long long youtubeRequestsBefore_ = 0; // Sent by readers that are gone
	bool shutDown_ = false;

	twitch::Endpoints twitchEndpoints_;
	youtube::Endpoints youtubeEndpoints_;
	// 0 keeps the usual value. Only a test sets these.
	long long retryFirstMs_ = 0;
	long long retryMaxMs_ = 0;
	long long signInPollMs_ = 0;
	long long keepaliveGraceMs_ = 0;
	long long youtubeMinPollMs_ = 0;
	long long youtubeNotLiveRetryMs_ = 0;
};

} // namespace rd
