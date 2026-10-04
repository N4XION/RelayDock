// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/chat_dock.h"

#include "app/app_context.h"
#include "app/chat_hub.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace rd {

namespace {

constexpr int kBadgePx = 14;

const char *providerIdOf(ChatPlatform platform)
{
	return platform == ChatPlatform::Twitch ? "twitch" : "youtube";
}

QString badgeUrl(ChatPlatform platform)
{
	return QStringLiteral("relaydock-badge:%1").arg(QString::fromUtf8(chatPlatformId(platform)));
}

QString css(const QColor &color)
{
	return color.name();
}

// A colour between two others. `share` is how much of `over` goes in, from 0 to 100.
QColor blend(const QColor &base, const QColor &over, int share)
{
	return QColor((base.red() * (100 - share) + over.red() * share) / 100,
		      (base.green() * (100 - share) + over.green() * share) / 100,
		      (base.blue() * (100 - share) + over.blue() * share) / 100);
}

} // namespace

QString chatStatusText(ChatPlatform platform, const ChatStatus &status)
{
	const QString name = QString::fromUtf8(chatPlatformName(platform));
	switch (status.state) {
	case ChatState::Off:
		return uiTextF("Chat.Status.Off", "{0}: off", ss(name));
	case ChatState::NotSetUp:
		return uiTextF("Chat.Status.NotSetUp", "{0}: not set up", ss(name));
	case ChatState::Connecting:
		return uiTextF("Chat.Status.Connecting", "{0}: connecting...", ss(name));
	case ChatState::Connected:
		if (status.account.empty())
			return uiTextF("Chat.Status.Connected", "{0}: reading the chat", ss(name));
		return uiTextF("Chat.Status.ConnectedAs", "{0}: reading the chat of {1}", ss(name), status.account);
	case ChatState::Waiting:
		return uiTextF("Chat.Status.Waiting", "{0}: not connected, trying again", ss(name));
	case ChatState::Stopped:
		return uiTextF("Chat.Status.Stopped", "{0}: stopped", ss(name));
	}
	return name;
}

const char *chatStatusTone(const ChatStatus &status)
{
	switch (status.state) {
	case ChatState::Connected:
		return "ok";
	case ChatState::Waiting:
		return "warning";
	case ChatState::Stopped:
		return "error";
	case ChatState::Off:
	case ChatState::NotSetUp:
	case ChatState::Connecting:
		break;
	}
	return "neutral";
}

ChatDockWidget::ChatDockWidget(UiHost &host, QWidget *parent)
	: QWidget(parent),
	  host_(host),
	  hub_(host.app().chat())
{
	setObjectName(QStringLiteral("rdRoot"));
	setAccessibleName(uiText("Chat.Dock.Name", "RelayDock chat"));

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	// One line per platform, with the buttons next to the first.
	auto *top = new QHBoxLayout();
	top->setSpacing(6);
	auto *statusColumn = new QVBoxLayout();
	statusColumn->setSpacing(2);
	for (const ChatPlatform platform : {ChatPlatform::Twitch, ChatPlatform::YouTube}) {
		PlatformRow row;
		row.platform = platform;
		row.badge = new ProviderBadge(this);
		row.badge->setBadgeSize(18);
		if (const IProvider *provider = host_.app().providers().find(providerIdOf(platform)))
			row.badge->setProvider(provider->info());
		row.text = new ElidedLabel(this);
		row.text->setObjectName(QStringLiteral("rdSmall"));
		auto *line = new QHBoxLayout();
		line->setSpacing(6);
		line->addWidget(row.badge);
		line->addWidget(row.text, 1);
		statusColumn->addLayout(line);
		rows_.push_back(row);
	}
	top->addLayout(statusColumn, 1);

	QToolButton *clear = makeToolButton(host_.theme(), "trash", uiText("Chat.Clear.Tip", "Empty the list"), this);
	QToolButton *settings = makeToolButton(host_.theme(), "settings", uiText("Chat.Settings.Tip", "Chat settings"), this);
	top->addWidget(clear, 0, Qt::AlignTop);
	top->addWidget(settings, 0, Qt::AlignTop);
	layout->addLayout(top);

	problem_ = new Banner(host_.theme(), this);
	problem_->hide();
	layout->addWidget(problem_);

	view_ = new QTextBrowser(this);
	view_->setAccessibleName(uiText("Chat.View.Name", "Live chat"));
	view_->setReadOnly(true);
	view_->setOpenLinks(false);
	view_->setOpenExternalLinks(false);
	view_->setUndoRedoEnabled(false);
	view_->document()->setMaximumBlockCount(static_cast<int>(hub_.timeline().capacity()));
	layout->addWidget(view_, 1);

	empty_ = makeLabel(QString(), "rdMuted", this);
	empty_->setWordWrap(true);
	empty_->setAlignment(Qt::AlignCenter);
	layout->addWidget(empty_);
	setUp_ = new QPushButton(uiText("Chat.SetUp", "Set up chat"), this);
	layout->addWidget(setUp_, 0, Qt::AlignHCenter);

	connect(clear, &QToolButton::clicked, this, [this] { hub_.clearTimeline(); });
	connect(settings, &QToolButton::clicked, this, [this] { host_.openSettings("chat"); });
	connect(setUp_, &QPushButton::clicked, this, [this] { host_.openSettings("chat"); });
	connect(problem_, &Banner::actionClicked, this, [this] { host_.openSettings("chat"); });

	connect(&hub_, &ChatHub::eventsAdded, this, &ChatDockWidget::append);
	connect(&hub_, &ChatHub::timelineCleared, this, &ChatDockWidget::rebuild);
	connect(&hub_, &ChatHub::statusChanged, this, &ChatDockWidget::refreshStatus);
	// New colours, a new text size, or logos switched on or off.
	connect(&host_.theme(), &Theme::changed, this, &ChatDockWidget::rebuild);
	connect(&host_.app(), &AppContext::configChanged, this, [this] {
		// The time in front of each line is part of the line, so every line is drawn again.
		if (host_.app().config().chat.showTime != showTime_)
			rebuild();
		refreshStatus();
	});

	host_.theme().attach(this);
	rebuild();
	refreshStatus();
}

void ChatDockWidget::refreshStatus()
{
	UserMessage firstProblem;
	const char *problemTone = "warning";
	for (const PlatformRow &row : rows_) {
		const ChatStatus status = hub_.status(row.platform);
		row.text->setFullText(chatStatusText(row.platform, status));
		setStyleProperty(row.text, "rdTone", QString::fromUtf8(chatStatusTone(status)));
		row.text->setToolTip(qs(status.message.text()));
		// What needs the user's attention: a reader that waits or has given up.
		const bool needsAttention = status.state == ChatState::Waiting || status.state == ChatState::Stopped;
		if (needsAttention && firstProblem.empty() && !status.message.empty()) {
			firstProblem = status.message;
			problemTone = status.state == ChatState::Stopped ? "error" : "warning";
		}
	}

	if (firstProblem.empty()) {
		problem_->hide();
	} else {
		problem_->setMessage(problemTone, firstProblem);
		problem_->setAction(uiText("Chat.Problem.Open", "Chat settings"));
		problem_->show();
	}
	refreshEmptyState();
}

void ChatDockWidget::refreshEmptyState()
{
	const bool hasLines = !hub_.timeline().events().empty();
	bool anySetUp = false;
	for (const PlatformRow &row : rows_) {
		const ChatState state = hub_.status(row.platform).state;
		anySetUp = anySetUp || state == ChatState::Connecting || state == ChatState::Connected || state == ChatState::Waiting;
	}

	view_->setVisible(hasLines);
	empty_->setVisible(!hasLines);
	setUp_->setVisible(!hasLines && !anySetUp);
	if (!hasLines) {
		empty_->setText(anySetUp ? uiText("Chat.Empty.Waiting", "Nothing yet. Comments, gifts and new subscribers appear here as they come in.")
					 : uiText("Chat.Empty.NotSetUp",
						  "Comments from Twitch and YouTube appear here in one list, with gifts, paid messages and new subscribers."));
	}
}

void ChatDockWidget::registerBadges()
{
	// The small platform mark in front of every line. It is a picture RelayDock draws itself,
	// handed to the text view under a name of its own. Nothing is loaded from anywhere.
	const qreal ratio = devicePixelRatioF();
	for (const ChatPlatform platform : {ChatPlatform::Twitch, ChatPlatform::YouTube}) {
		const IProvider *provider = host_.app().providers().find(providerIdOf(platform));
		if (!provider)
			continue;
		const QPixmap pixmap = providerBadgePixmap(provider->info(), ProviderBadge::logosEnabled(), kBadgePx, ratio, font());
		view_->document()->addResource(QTextDocument::ImageResource, QUrl(badgeUrl(platform)), pixmap);
	}
}

QString ChatDockWidget::lineHtml(const ChatEvent &event) const
{
	Theme &theme = host_.theme();
	const QString muted = css(theme.color(Theme::Tone::Muted));

	QString html;
	if (host_.app().config().chat.showTime) {
		const QDateTime when = event.timeMs > 0 ? QDateTime::fromMSecsSinceEpoch(event.timeMs) : QDateTime::currentDateTime();
		html += QStringLiteral("<span style=\"color:%1\">%2</span> ").arg(muted, when.toString(QStringLiteral("HH:mm")));
	}
	html += QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%2\" style=\"vertical-align:middle\"> ")
			.arg(badgeUrl(event.platform))
			.arg(kBadgePx);

	// Everything a viewer or a platform wrote goes in as text, with every character that
	// means something in HTML turned into its harmless form.
	const QString author = qs(event.author).toHtmlEscaped();
	const QString text = qs(event.text).toHtmlEscaped();
	const QString headline = qs(event.headline).toHtmlEscaped();

	if (isHighlight(event)) {
		// The tinted line marks it. The words keep the text colour, which reads on any theme.
		html += QStringLiteral("<b>%1</b>").arg(headline.isEmpty() ? author : headline);
		// The platform's own sentence usually names the viewer. Add the name when it does not.
		if (!headline.isEmpty() && !author.isEmpty() && !headline.contains(author))
			html += QStringLiteral(" <span style=\"color:%1\">%2</span>").arg(muted, author);
		if (!text.isEmpty())
			html += QStringLiteral("<br>") + text;
		return html;
	}

	QString mark;
	if (event.fromBroadcaster)
		mark = uiText("Chat.Mark.Host", "host");
	else if (event.fromModerator)
		mark = uiText("Chat.Mark.Mod", "mod");
	html += QStringLiteral("<b>%1</b>").arg(author);
	if (!mark.isEmpty())
		html += QStringLiteral(" <span style=\"color:%1\">(%2)</span>").arg(muted, mark.toHtmlEscaped());
	html += QStringLiteral(": ") + text;
	return html;
}

void ChatDockWidget::append(const std::vector<ChatEvent> &events)
{
	if (events.empty())
		return;

	QScrollBar *bar = view_->verticalScrollBar();
	// Follow new lines only while the user looks at the newest one.
	const bool atEnd = !view_->isVisible() || bar->value() >= bar->maximum() - 4;

	Theme &theme = host_.theme();
	const QColor background = view_->palette().color(QPalette::Base);
	const QColor highlight = blend(background, theme.color(Theme::Tone::Accent), 22);

	QTextCursor cursor(view_->document());
	cursor.movePosition(QTextCursor::End);
	cursor.beginEditBlock();
	for (const ChatEvent &event : events) {
		QTextBlockFormat format;
		format.setTopMargin(2);
		format.setBottomMargin(2);
		if (isHighlight(event))
			format.setBackground(highlight);
		// The first line goes into the block an empty document already has.
		if (view_->document()->isEmpty())
			cursor.setBlockFormat(format);
		else
			cursor.insertBlock(format);
		cursor.insertHtml(lineHtml(event));
	}
	cursor.endEditBlock();

	refreshEmptyState();
	if (atEnd)
		bar->setValue(bar->maximum());
}

void ChatDockWidget::rebuild()
{
	showTime_ = host_.app().config().chat.showTime;
	view_->clear();
	registerBadges();
	const auto &events = hub_.timeline().events();
	append(std::vector<ChatEvent>(events.begin(), events.end()));
	refreshEmptyState();
}

} // namespace rd
