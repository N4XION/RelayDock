// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "app/chat_hub.h"

#include "app/app_context.h"
#include "build_info.h"
#include "chat/chat_accounts.h"
#include "utils/i18n.h"
#include "utils/log.h"
#include "utils/strings.h"

#include <format>

namespace rd {

ChatHub::ChatHub(AppContext &app, QObject *parent) : QObject(parent), app_(app) {}

ChatHub::~ChatHub()
{
	shutdown();
}

void ChatHub::shutdown()
{
	if (shutDown_)
		return;
	shutDown_ = true;
	// Each of these ends its thread within about a tenth of a second.
	signIn_.reset();
	twitch_.reset();
	youtube_.reset();
}

std::string ChatHub::userAgent() const
{
	return std::format("RelayDock/{}", buildInfo().versionNumeric);
}

void ChatHub::clearTimeline()
{
	timeline_.clear();
	Q_EMIT timelineCleared();
}

// ---- What the readers report ------------------------------------------------------------------

ChatWorker::Callbacks ChatHub::callbacksFor(ChatPlatform platform)
{
	// Both run on the reader's thread. Qt drops what is still queued when this object goes.
	ChatWorker::Callbacks callbacks;
	callbacks.events = [this](std::vector<ChatEvent> events) {
		QMetaObject::invokeMethod(
			this, [this, events = std::move(events)] { onEvents(events); }, Qt::QueuedConnection);
	};
	callbacks.status = [this, platform](const ChatStatus &status) {
		QMetaObject::invokeMethod(
			this, [this, platform, status] { onStatus(platform, status); }, Qt::QueuedConnection);
	};
	return callbacks;
}

void ChatHub::onEvents(const std::vector<ChatEvent> &events)
{
	if (shutDown_)
		return;
	std::vector<ChatEvent> added;
	timeline_.add(events, &added);
	if (!added.empty())
		Q_EMIT eventsAdded(added);
}

void ChatHub::onStatus(ChatPlatform platform, const ChatStatus &status)
{
	if (shutDown_)
		return;
	// A status that was queued by a reader that is gone by now says nothing about today.
	if (platform == ChatPlatform::Twitch && twitch_)
		twitchStatus_ = status;
	else if (platform == ChatPlatform::YouTube && youtube_)
		youtubeStatus_ = status;
	else
		return;
	Q_EMIT statusChanged();
}

ChatStatus ChatHub::status(ChatPlatform platform) const
{
	if (platform == ChatPlatform::Twitch) {
		if (twitch_)
			return twitchStatus_;
		ChatStatus status;
		status.account = app_.config().chat.twitchLogin;
		if (!twitch::validClientId(twitchClientId())) {
			status.state = ChatState::NotSetUp;
			status.message = twitchNoClientIdMessage();
		} else if (!twitchSignedIn()) {
			status.state = ChatState::NotSetUp;
			status.message = twitchSignInNeededMessage();
		}
		return status;
	}

	if (youtube_)
		return youtubeStatus_;
	ChatStatus status;
	if (!youtubeKeySaved()) {
		status.state = ChatState::NotSetUp;
		status.message = youtubeNoKeyMessage();
	}
	return status;
}

// ---- Starting and stopping --------------------------------------------------------------------

void ChatHub::apply()
{
	if (shutDown_)
		return;
	const ChatConfig &chat = app_.config().chat;

	const bool wantTwitch = chat.twitchEnabled && twitchSignedIn() && twitch::validClientId(twitchClientId());
	if (wantTwitch) {
		if (!twitch_)
			startTwitch();
	} else if (twitch_) {
		twitch_.reset();
		twitchStatus_ = ChatStatus{};
	}

	std::string videoId;
	const bool wantYouTube = chat.youtubeEnabled && youtubeKeySaved() && youtube::parseVideoId(chat.youtubeVideo, videoId);
	if (wantYouTube) {
		if (!youtube_)
			startYouTube();
	} else if (youtube_) {
		youtubeRequestsBefore_ += youtube_->requests();
		youtube_.reset();
		youtubeStatus_ = ChatStatus{};
	}
	Q_EMIT statusChanged();
}

// ---- Twitch -----------------------------------------------------------------------------------

std::string ChatHub::twitchClientId() const
{
	const std::string &own = app_.config().chat.twitchClientId;
	return own.empty() ? std::string(buildInfo().twitchClientId) : own;
}

bool ChatHub::twitchSignedIn() const
{
	return app_.vault().has(twitchChatCredential());
}

TwitchChatConfig ChatHub::twitchConfig() const
{
	TwitchChatConfig config;
	config.endpoints = twitchEndpoints_;
	config.clientId = twitchClientId();
	config.userAgent = userAgent();
	if (retryFirstMs_ > 0)
		config.retryFirstMs = retryFirstMs_;
	if (retryMaxMs_ > 0)
		config.retryMaxMs = retryMaxMs_;
	if (signInPollMs_ > 0)
		config.signInPollMs = signInPollMs_;
	if (keepaliveGraceMs_ > 0)
		config.keepaliveGraceMs = keepaliveGraceMs_;
	return config;
}

TwitchTokenStore ChatHub::twitchStore()
{
	// These run on a reader's thread. The vault may be called from any thread.
	SecretVault *vault = &app_.vault();
	TwitchTokenStore store;
	store.load = [vault] {
		SecretString value;
		return vault->get(twitchChatCredential(), value).ok() ? value.reveal() : std::string();
	};
	store.save = [vault](const std::string &token) { vault->set(twitchChatCredential(), SecretString(token)); };
	store.clear = [vault] { vault->remove(twitchChatCredential()); };
	return store;
}

void ChatHub::startTwitch()
{
	twitchStatus_ = ChatStatus{};
	twitchStatus_.state = ChatState::Connecting;
	twitchStatus_.account = app_.config().chat.twitchLogin;
	twitch_ = std::make_unique<TwitchChat>(twitchConfig(), twitchStore(), callbacksFor(ChatPlatform::Twitch));
	twitch_->start();
}

bool ChatHub::twitchSignInRunning() const
{
	return signIn_ && signIn_->running();
}

void ChatHub::startTwitchSignIn()
{
	if (shutDown_)
		return;

	TwitchSignIn::Callbacks callbacks;
	callbacks.code = [this](const twitch::DeviceCode &code) {
		QMetaObject::invokeMethod(
			this,
			[this, code] {
				Q_EMIT twitchSignInCode(QString::fromStdString(code.userCode), QString::fromStdString(code.verificationUri));
			},
			Qt::QueuedConnection);
	};
	callbacks.finished = [this](bool ok, const std::string &login, const UserMessage &problem) {
		QMetaObject::invokeMethod(
			this,
			[this, ok, login, problem] {
				if (shutDown_)
					return;
				if (ok) {
					app_.config().chat.twitchLogin = login;
					app_.config().chat.twitchEnabled = true;
					app_.saveConfig();
					// A reader that had given up starts over with the new sign-in.
					twitch_.reset();
					twitchStatus_ = ChatStatus{};
					apply();
				}
				Q_EMIT twitchSignInFinished(ok, QString::fromStdString(login), problem);
				Q_EMIT statusChanged();
			},
			Qt::QueuedConnection);
	};

	signIn_ = std::make_unique<TwitchSignIn>(twitchConfig(), twitchStore(), std::move(callbacks));
	signIn_->start();
}

void ChatHub::cancelTwitchSignIn()
{
	signIn_.reset();
}

void ChatHub::signOutTwitch()
{
	signIn_.reset();
	twitch_.reset();
	twitchStatus_ = ChatStatus{};
	app_.vault().remove(twitchChatCredential());
	app_.config().chat.twitchLogin.clear();
	app_.saveConfig();
	logInfo("Signed out of Twitch chat on this PC.");
	Q_EMIT statusChanged();
}

void ChatHub::setTwitchEnabled(bool enabled)
{
	app_.config().chat.twitchEnabled = enabled;
	app_.saveConfig();
	apply();
}

// ---- YouTube ----------------------------------------------------------------------------------

bool ChatHub::youtubeKeySaved() const
{
	return app_.vault().has(youtubeChatCredential());
}

SecretVault::SetResult ChatHub::setYouTubeKey(const SecretString &key)
{
	const SecretVault::SetResult result = app_.vault().set(youtubeChatCredential(), key);
	// A reader that runs with the old key starts over with the new one.
	if (youtube_) {
		youtubeRequestsBefore_ += youtube_->requests();
		youtube_.reset();
		youtubeStatus_ = ChatStatus{};
	}
	apply();
	return result;
}

void ChatHub::removeYouTubeKey()
{
	if (youtube_) {
		youtubeRequestsBefore_ += youtube_->requests();
		youtube_.reset();
	}
	youtubeStatus_ = ChatStatus{};
	app_.vault().remove(youtubeChatCredential());
	app_.config().chat.youtubeEnabled = false;
	app_.saveConfig();
	Q_EMIT statusChanged();
}

void ChatHub::startYouTube()
{
	SecretString key;
	app_.vault().get(youtubeChatCredential(), key);

	YouTubeChatConfig config;
	config.endpoints = youtubeEndpoints_;
	config.apiKey = key.reveal();
	youtube::parseVideoId(app_.config().chat.youtubeVideo, config.videoId);
	config.userAgent = userAgent();
	config.minPollMs = youtubeMinPollMs_ > 0 ? youtubeMinPollMs_ : app_.config().chat.youtubePollSeconds * 1000LL;
	if (youtubeNotLiveRetryMs_ > 0)
		config.notLiveRetryMs = youtubeNotLiveRetryMs_;
	if (retryFirstMs_ > 0)
		config.retryFirstMs = retryFirstMs_;
	if (retryMaxMs_ > 0)
		config.retryMaxMs = retryMaxMs_;

	youtubeStatus_ = ChatStatus{};
	youtubeStatus_.state = ChatState::Connecting;
	youtube_ = std::make_unique<YouTubeChat>(callbacksFor(ChatPlatform::YouTube));
	youtube_->start(std::move(config));
}

bool ChatHub::connectYouTube(const std::string &videoInput, UserMessage &problem)
{
	if (!youtubeKeySaved()) {
		problem = youtubeNoKeyMessage();
		return false;
	}
	std::string videoId;
	if (!youtube::parseVideoId(videoInput, videoId)) {
		problem.what = loc("Chat.YouTube.BadLink", "This is not a link to a YouTube stream.");
		problem.action = loc("Chat.YouTube.BadLink.Action",
				     "Paste the link to your stream from your browser, or from YouTube Studio, Go live, Share.");
		return false;
	}

	app_.config().chat.youtubeVideo = trim(videoInput);
	app_.config().chat.youtubeEnabled = true;
	app_.saveConfig();
	if (youtube_) {
		youtubeRequestsBefore_ += youtube_->requests();
		youtube_.reset();
		youtubeStatus_ = ChatStatus{};
	}
	apply();
	return true;
}

void ChatHub::disconnectYouTube()
{
	app_.config().chat.youtubeEnabled = false;
	app_.saveConfig();
	apply();
}

unsigned long long ChatHub::youtubeRequests() const
{
	return youtubeRequestsBefore_ + (youtube_ ? youtube_->requests() : 0);
}

#ifdef RELAYDOCK_TEST_HOOKS
void ChatHub::setTestSetup(const TestSetup &setup)
{
	signIn_.reset();
	twitch_.reset();
	youtube_.reset();
	twitchStatus_ = ChatStatus{};
	youtubeStatus_ = ChatStatus{};
	twitchEndpoints_ = setup.twitch;
	youtubeEndpoints_ = setup.youtube;
	retryFirstMs_ = setup.retryFirstMs;
	retryMaxMs_ = setup.retryMaxMs;
	signInPollMs_ = setup.signInPollMs;
	keepaliveGraceMs_ = setup.keepaliveGraceMs;
	youtubeMinPollMs_ = setup.youtubeMinPollMs;
	youtubeNotLiveRetryMs_ = setup.youtubeNotLiveRetryMs;
}
#endif

} // namespace rd
