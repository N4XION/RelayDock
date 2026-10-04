// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/destination_card.h"

#include "app/app_context.h"
#include "app/background_tasks.h"
#include "app/diagnostics_service.h"
#include "app/performance_monitor.h"
#include "network/stream_url.h"
#include "outputs/output_manager.h"
#include "utils/strings.h"

#include <QApplication>
#include <QCheckBox>
#include <QDrag>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace rd {

namespace {

QString videoSummary(const EffectiveVideo &video)
{
	return uiTextF("Card.VideoSummary", "{0} at {1} FPS, {2}", formatResolution(video.width, video.height),
		       static_cast<int>(std::lround(video.fps)), formatBitrate(video.bitrateKbps));
}

} // namespace

DestinationCard::DestinationCard(UiHost &host, std::string id, QWidget *parent)
	: QFrame(parent), host_(host), id_(std::move(id))
{
	setObjectName(QStringLiteral("rdCard"));
	setFocusPolicy(Qt::StrongFocus);
	Theme &theme = host_.theme();
	const ThemeMetrics &metrics = theme.metrics();

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(metrics.padding, metrics.padding, metrics.padding, metrics.padding);
	outer->setSpacing(metrics.spacing);

	// ---- Header row ----------------------------------------------------------------------------
	auto *header = new QHBoxLayout();
	header->setSpacing(metrics.spacing);

	grip_ = new QLabel(this);
	grip_->setCursor(Qt::OpenHandCursor);
	grip_->setToolTip(uiText("Card.Grip.Tip", "Drag to reorder"));
	header->addWidget(grip_);

	enabled_ = new QCheckBox(this);
	enabled_->setToolTip(uiText("Card.Enabled.Tip", "Include this destination in Start All Enabled"));
	enabled_->setAccessibleName(uiText("Card.Enabled.Name", "Enabled"));
	header->addWidget(enabled_);

	badge_ = new ProviderBadge(this);
	header->addWidget(badge_);

	name_ = new ElidedLabel(this);
	name_->setObjectName(QStringLiteral("rdTitle"));
	header->addWidget(name_, 1);

	pill_ = new Pill(this);
	header->addWidget(pill_);

	startStop_ = makeToolButton(theme, "play", QString(), this);
	header->addWidget(startStop_);

	menuButton_ = makeToolButton(theme, "ellipsis", uiText("Card.Menu.Tip", "More actions"), this);
	menuButton_->setPopupMode(QToolButton::InstantPopup);
	header->addWidget(menuButton_);
	outer->addLayout(header);

	// What it streams, or how the stream is doing. Its own row, so a narrow dock keeps it whole.
	summary_ = new ElidedLabel(this);
	summary_->setObjectName(QStringLiteral("rdSmall"));
	outer->addWidget(summary_);

	// ---- Details, shown in the expanded view ---------------------------------------------------
	details_ = new QWidget(this);
	auto *detailsLayout = new QVBoxLayout(details_);
	detailsLayout->setContentsMargins(0, 0, 0, 0);
	detailsLayout->setSpacing(2);
	serverLine_ = new ElidedLabel(details_);
	serverLine_->setObjectName(QStringLiteral("rdSmall"));
	encoderLine_ = new ElidedLabel(details_);
	encoderLine_->setObjectName(QStringLiteral("rdSmall"));
	statusLine_ = new ElidedLabel(details_);
	statusLine_->setObjectName(QStringLiteral("rdSmall"));
	detailsLayout->addWidget(serverLine_);
	detailsLayout->addWidget(encoderLine_);
	detailsLayout->addWidget(statusLine_);
	outer->addWidget(details_);

	// ---- Messages. Always visible when there is something to say. ------------------------------
	problem_ = new Banner(theme, this);
	problem_->hide();
	outer->addWidget(problem_);
	connect(problem_, &Banner::actionClicked, this, [this] { host_.editDestination(id_); });

	testResult_ = new Banner(theme, this);
	testResult_->hide();
	outer->addWidget(testResult_);

	connect(enabled_, &QCheckBox::toggled, this, [this](bool checked) {
		DestinationConfig *config = host_.app().config().findDestination(id_);
		if (!config || config->enabled == checked)
			return;
		config->enabled = checked;
		host_.app().notifyConfigChanged();
	});
	connect(startStop_, &QToolButton::clicked, this, &DestinationCard::toggleStartStop);

	auto applyTheme = [this] {
		Theme &t = host_.theme();
		grip_->setPixmap(t.pixmap("grip-vertical", t.color(Theme::Tone::Muted), t.metrics().iconPx, devicePixelRatioF()));
		badge_->setBadgeSize(t.metrics().fontPx * 2 + 2);
	};
	applyTheme();
	connect(&theme, &Theme::changed, this, applyTheme);

	buildMenu();
}

void DestinationCard::setExpanded(bool expanded)
{
	expanded_ = expanded;
	details_->setVisible(expanded_);
}

// ---- Menu ----------------------------------------------------------------------------------------

void DestinationCard::buildMenu()
{
	auto *menu = new QMenu(menuButton_);
	menuButton_->setMenu(menu);

	// Built when it opens, so every entry reflects the state at that moment.
	connect(menu, &QMenu::aboutToShow, this, [this, menu] {
		menu->clear();
		AppContext &app = host_.app();
		const DestinationConfig *config = app.config().findDestination(id_);
		if (!config)
			return;
		const IProvider *provider = app.providers().find(config->provider);
		const DestinationRuntime runtime = app.outputs().runtime(id_);
		Theme &theme = host_.theme();

		menu->addAction(theme.icon("pencil"), uiText("Card.Menu.Edit", "Edit..."), this, [this] { host_.editDestination(id_); });
		menu->addAction(theme.icon("copy"), uiText("Card.Menu.Duplicate", "Duplicate"), this, [this] {
			if (host_.app().duplicateDestination(id_))
				host_.app().notifyConfigChanged();
		});
		menu->addSeparator();

		QAction *test = menu->addAction(theme.icon("wifi"), uiText("Card.Menu.TestConnection", "Test connection"), this,
						&DestinationCard::startTestConnection);
		test->setToolTip(uiText("Card.Menu.TestConnection.Tip",
					"Checks that the server answers. It cannot check your stream key."));
		if (provider && provider->info().testSupport == TestSupport::PrivateStream) {
			QAction *testStream = menu->addAction(theme.icon("radio"), uiText("Card.Menu.TestStream", "Start test stream"), this,
							      [this] { host_.app().outputs().start(id_, true); });
			testStream->setEnabled(!runtime.active());
			testStream->setToolTip(uiText("Card.Menu.TestStream.Tip",
						      "Sends a real stream that the platform does not show to viewers."));
		}
		QAction *reconnect = menu->addAction(theme.icon("refresh-cw"), uiText("Card.Menu.Reconnect", "Reconnect"), this,
						     [this] { host_.app().outputs().reconnect(id_); });
		reconnect->setEnabled(runtime.phase == DestinationPhase::Live || runtime.phase == DestinationPhase::Reconnecting);
		menu->addSeparator();

		const auto &list = app.config().destinations;
		const bool first = !list.empty() && list.front().id == id_;
		const bool last = !list.empty() && list.back().id == id_;
		QAction *up = menu->addAction(theme.icon("arrow-up"), uiText("Card.Menu.MoveUp", "Move up"), this, [this] { move(-1); });
		up->setEnabled(!first);
		QAction *down =
			menu->addAction(theme.icon("arrow-down"), uiText("Card.Menu.MoveDown", "Move down"), this, [this] { move(1); });
		down->setEnabled(!last);
		menu->addSeparator();

		QAction *remove = menu->addAction(theme.icon("trash"), uiText("Card.Menu.Remove", "Remove..."), this,
						  &DestinationCard::removeAfterConfirm);
		remove->setEnabled(!runtime.active());
		if (runtime.active())
			remove->setToolTip(uiText("Card.Menu.Remove.Active", "Stop the destination before you remove it."));
		menu->setToolTipsVisible(true);
	});
}

void DestinationCard::move(int delta)
{
	AppContext &app = host_.app();
	const auto &list = app.config().destinations;
	for (size_t i = 0; i < list.size(); ++i) {
		if (list[i].id != id_)
			continue;
		if (app.moveDestination(id_, static_cast<int>(i) + delta))
			app.notifyConfigChanged();
		return;
	}
}

void DestinationCard::removeAfterConfirm()
{
	AppContext &app = host_.app();
	const DestinationConfig *config = app.config().findDestination(id_);
	if (!config)
		return;

	const QString question = uiTextF("Card.Remove.Question", "Remove {0}?", config->name);
	const QString detail = uiText("Card.Remove.Detail",
				      "RelayDock deletes this destination and its saved stream key from Windows Credential Manager. Your key on the platform stays valid.");
	QMessageBox box(QMessageBox::Question, uiText("Card.Remove.Title", "Remove destination"), question,
			QMessageBox::Yes | QMessageBox::Cancel, host_.dialogParent());
	box.setInformativeText(detail);
	box.setDefaultButton(QMessageBox::Cancel);
	if (box.exec() != QMessageBox::Yes)
		return;

	// The dock rebuilds its cards after this, which deletes this card. Do nothing afterwards.
	if (app.removeDestination(id_))
		app.notifyConfigChanged();
}

// ---- Actions ---------------------------------------------------------------------------------------

void DestinationCard::toggleStartStop()
{
	OutputManager &outputs = host_.app().outputs();
	const DestinationRuntime runtime = outputs.runtime(id_);
	if (runtime.active() || runtime.phase == DestinationPhase::Stopping)
		outputs.stop(id_);
	else
		outputs.start(id_);
}

void DestinationCard::startTestConnection()
{
	AppContext &app = host_.app();
	const DestinationConfig *config = app.config().findDestination(id_);
	const IProvider *provider = config ? app.providers().find(config->provider) : nullptr;
	if (!config || !provider)
		return;

	const Endpoint endpoint = provider->endpoint(*config);
	const StreamUrlResult parsed = parseStreamUrl(endpoint.serverUrl);
	if (!parsed.ok()) {
		ReachResult invalid;
		invalid.status = ReachStatus::InvalidAddress;
		testResult_->setMessage("error", describeReachability(config->name, invalid, false));
		testResult_->show();
		return;
	}

	if (!tester_) {
		tester_ = new ConnectionTester(this);
		connect(tester_, &ConnectionTester::finished, this, [this](const ReachResult &result) {
			const DestinationConfig *current = host_.app().config().findDestination(id_);
			const IProvider *currentProvider = current ? host_.app().providers().find(current->provider) : nullptr;
			const bool tls = current && currentProvider && currentProvider->endpoint(*current).tls;
			const UserMessage message = describeReachability(current ? current->name : std::string(), result, tls);
			testResult_->setMessage(result.status == ReachStatus::Reachable ? "ok" : "error", message);
			testResult_->show();
			// The answer describes one moment. Take it away again after a while.
			QTimer::singleShot(20000, testResult_, [this] { testResult_->hide(); });
		});
	}
	testResult_->setMessage("neutral", uiTextF("Card.Test.Running", "Testing the connection to {0}...", parsed.url.host));
	testResult_->show();
	tester_->start(parsed.url.host, parsed.url.effectivePort());
}

// ---- Display ---------------------------------------------------------------------------------------

QString DestinationCard::statsLine() const
{
	const DestinationStats stats = host_.app().outputs().stats(id_);
	const double dropped = host_.app().performance().dropPercent(id_);
	const QString droppedText = dropped < 0.0 ? QString::number(stats.droppedPercent, 'f', 1) : QString::number(dropped, 'f', 1);
	return uiTextF("Card.LiveSummary", "{0}, {1}% dropped, {2}", formatBitrate(stats.bitrateKbps), ss(droppedText),
		       formatDuration(stats.liveSeconds));
}

void DestinationCard::refreshStats()
{
	const DestinationRuntime runtime = host_.app().outputs().runtime(id_);
	if (runtime.phase == DestinationPhase::Live)
		summary_->setFullText(statsLine());
}

void DestinationCard::refresh(const EffectiveDestination *planned)
{
	AppContext &app = host_.app();
	const DestinationConfig *config = app.config().findDestination(id_);
	if (!config)
		return;
	if (planned) {
		planned_ = *planned;
		hasPlanned_ = true;
	}

	const IProvider *provider = app.providers().find(config->provider);
	const DestinationRuntime runtime = app.outputs().runtime(id_);
	const DestinationSessionInfo session = app.outputs().sessionInfo(id_);
	const std::vector<ValidationIssue> issues = app.validateDestination(id_);
	Theme &theme = host_.theme();

	name_->setFullText(qs(config->name));
	if (provider)
		badge_->setProvider(qs(provider->info().monogram), qs(provider->info().accentColor));
	else
		badge_->setProvider(QStringLiteral("?"), QStringLiteral("#606060"));
	badge_->setToolTip(provider ? qs(provider->info().displayName) : qs(config->provider));

	{
		const QSignalBlocker blocker(enabled_);
		enabled_->setChecked(config->enabled);
	}

	// ---- Status pill and start/stop button ---------------------------------------------------
	const bool setupError = hasErrors(issues);
	const char *tone = "neutral";
	QString status;
	switch (runtime.phase) {
	case DestinationPhase::Idle:
		if (setupError) {
			tone = "warning";
			status = uiText("Card.Status.Setup", "SETUP NEEDED");
		} else if (!config->enabled) {
			status = uiText("Card.Status.Off", "OFF");
		} else {
			status = uiText("Card.Status.Ready", "READY");
		}
		break;
	case DestinationPhase::Starting:
		tone = "warning";
		status = uiText("Card.Status.Connecting", "CONNECTING");
		break;
	case DestinationPhase::Live:
		tone = "ok";
		status = runtime.privateTest ? uiText("Card.Status.Test", "TEST STREAM") : uiText("Card.Status.Live", "LIVE");
		break;
	case DestinationPhase::Reconnecting:
		tone = "warning";
		status = uiText("Card.Status.Reconnecting", "RECONNECTING");
		break;
	case DestinationPhase::Stopping:
		status = uiText("Card.Status.Stopping", "STOPPING");
		break;
	case DestinationPhase::Failed:
		tone = "error";
		status = uiText("Card.Status.Failed", "FAILED");
		break;
	}
	pill_->setStatus(tone, status);
	setPulsing(runtime.phase == DestinationPhase::Starting || runtime.phase == DestinationPhase::Reconnecting);
	setStyleProperty(this, "rdPhase", QString::fromUtf8(destinationPhaseName(runtime.phase)));

	const bool stoppable = runtime.active() || runtime.phase == DestinationPhase::Stopping;
	if (stoppable != showsStop_ || startStop_->toolTip().isEmpty()) {
		showsStop_ = stoppable;
		startStop_->setIcon(theme.icon(stoppable ? "square" : "play"));
	}
	const QString startTip = stoppable ? (runtime.phase == DestinationPhase::Stopping
						      ? uiText("Card.Stop.Force.Tip", "Stop now without waiting")
						      : uiTextF("Card.Stop.Tip", "Stop {0}", config->name))
					   : uiTextF("Card.Start.Tip", "Start {0}", config->name);
	startStop_->setToolTip(startTip);
	startStop_->setAccessibleName(startTip);

	// ---- Summary line ---------------------------------------------------------------------------
	const EffectiveDestination *shown = session.hasOutput ? &session.effective : (hasPlanned_ ? &planned_ : nullptr);
	if (runtime.phase == DestinationPhase::Live) {
		summary_->setFullText(statsLine());
	} else if (runtime.phase == DestinationPhase::Reconnecting) {
		summary_->setFullText(runtime.reconnectInSec > 0
					      ? uiTextF("Card.Reconnect.In", "Attempt {0}, next try in {1} s", runtime.reconnectAttempt,
							runtime.reconnectInSec)
					      : uiTextF("Card.Reconnect.Now", "Attempt {0}", runtime.reconnectAttempt));
	} else if (shown && shown->usable()) {
		summary_->setFullText(videoSummary(shown->video));
	} else {
		summary_->setFullText(provider ? qs(provider->info().displayName) : QString());
	}

	// ---- Details --------------------------------------------------------------------------------
	serverLine_->setFullText(uiTextF("Card.Server", "Server: {0}", safeServerText(app, *config)));

	if (shown && shown->usable()) {
		const StreamContext context = app.streamContext();
		const VideoEncoderCaps *caps = context.findVideoEncoder(shown->video.encoderId);
		const std::string encoderName = caps ? caps->displayName : shown->video.encoderId;
		QString line = uiTextF("Card.Encoder", "Encoder: {0}, {1}", encoderName,
				       shown->video.orientation == Orientation::Vertical ? loc("Card.Vertical", "vertical 9:16")
											 : loc("Card.Horizontal", "horizontal 16:9"));
		if (!session.sharedWith.empty()) {
			std::vector<std::string> names;
			for (const std::string &other : session.sharedWith) {
				const DestinationConfig *otherConfig = app.config().findDestination(other);
				names.push_back(otherConfig ? otherConfig->name : other);
			}
			line += QStringLiteral(". ") + uiTextF("Card.Shared", "Shared with {0}", join(names, ", "));
		}
		encoderLine_->setFullText(line);
		encoderLine_->show();
	} else {
		encoderLine_->hide();
	}

	if (runtime.phase == DestinationPhase::Live && runtime.reconnectsThisRun > 0) {
		statusLine_->setFullText(uiTextF("Card.Reconnects", "Reconnects during this stream: {0}", runtime.reconnectsThisRun));
		statusLine_->show();
	} else if (!shown || shown->notes.empty()) {
		statusLine_->hide();
	} else {
		statusLine_->setFullText(qs(shown->notes.front()));
		statusLine_->show();
	}

	// ---- Problems -------------------------------------------------------------------------------
	if (runtime.phase == DestinationPhase::Failed && !runtime.error.empty()) {
		problem_->setMessage("error", runtime.error);
		problem_->setAction(uiText("Card.Problem.Edit", "Edit"));
		problem_->show();
	} else if (!runtime.active() && setupError) {
		for (const ValidationIssue &issue : issues) {
			if (issue.severity != Severity::Error)
				continue;
			problem_->setMessage("warning", issue.message);
			break;
		}
		problem_->setAction(uiText("Card.Problem.Edit", "Edit"));
		problem_->show();
	} else {
		problem_->hide();
	}

	details_->setVisible(expanded_);
}

void DestinationCard::setPulsing(bool on)
{
	// The only animation RelayDock has. Potato Mode, "Reduce motion" and the animation switch
	// all turn it off.
	if (!on || !host_.theme().animations()) {
		if (pulse_)
			pulse_->stop();
		if (pillEffect_)
			pillEffect_->setOpacity(1.0);
		return;
	}
	if (!pillEffect_) {
		pillEffect_ = new QGraphicsOpacityEffect(pill_);
		pill_->setGraphicsEffect(pillEffect_);
		pulse_ = new QPropertyAnimation(pillEffect_, "opacity", this);
		pulse_->setDuration(1600);
		pulse_->setKeyValueAt(0.0, 1.0);
		pulse_->setKeyValueAt(0.5, 0.45);
		pulse_->setKeyValueAt(1.0, 1.0);
		pulse_->setLoopCount(-1);
	}
	if (pulse_->state() != QAbstractAnimation::Running)
		pulse_->start();
}

// ---- Mouse and keyboard ----------------------------------------------------------------------------

void DestinationCard::mousePressEvent(QMouseEvent *event)
{
	dragArmed_ = event->button() == Qt::LeftButton && grip_->geometry().adjusted(-4, -8, 4, 8).contains(event->pos());
	dragStart_ = event->pos();
	QFrame::mousePressEvent(event);
}

void DestinationCard::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragArmed_ || !(event->buttons() & Qt::LeftButton) ||
	    (event->pos() - dragStart_).manhattanLength() < QApplication::startDragDistance()) {
		QFrame::mouseMoveEvent(event);
		return;
	}
	dragArmed_ = false;

	auto *drag = new QDrag(this);
	auto *mime = new QMimeData();
	mime->setData(QString::fromLatin1(kDragMimeType), QByteArray::fromStdString(id_));
	drag->setMimeData(mime);
	drag->setPixmap(grab().scaledToWidth(std::min(width(), 260), Qt::SmoothTransformation));
	drag->setHotSpot(QPoint(12, 12));
	drag->exec(Qt::MoveAction);
}

void DestinationCard::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		host_.editDestination(id_);
}

void DestinationCard::keyPressEvent(QKeyEvent *event)
{
	// Keyboard users reorder with Alt+Up and Alt+Down and open the editor with Enter.
	if (event->modifiers() == Qt::AltModifier && event->key() == Qt::Key_Up) {
		move(-1);
		return;
	}
	if (event->modifiers() == Qt::AltModifier && event->key() == Qt::Key_Down) {
		move(1);
		return;
	}
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
		host_.editDestination(id_);
		return;
	}
	QFrame::keyPressEvent(event);
}

} // namespace rd
