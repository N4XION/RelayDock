// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/settings_dialog.h"

#include "app/app_context.h"
#include "app/diagnostics_service.h"
#include "app/performance_monitor.h"
#include "encoders/encode_plan.h"
#include "outputs/output_manager.h"
#include "outputs/vertical_canvas.h"
#include "performance/optimizer_text.h"
#include "utils/strings.h"
#include "utils/uuid.h"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace rd {

namespace {

// Removes everything from a layout, so a page can rebuild a list it shows.
void clearLayout(QLayout *layout)
{
	while (QLayoutItem *item = layout->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		if (QLayout *child = item->layout())
			clearLayout(child);
		delete item;
	}
}

QString percentText(double value, int decimals)
{
	return value < 0.0 ? QStringLiteral("-") : QStringLiteral("%1%").arg(value, 0, 'f', decimals);
}

QFrame *makePanel(QWidget *parent)
{
	auto *panel = new QFrame(parent);
	panel->setObjectName(QStringLiteral("rdPanel"));
	return panel;
}

} // namespace

const std::vector<std::string> &SettingsDialog::pageIds()
{
	static const std::vector<std::string> ids = {"general",     "platforms",    "streaming",   "video",   "audio",    "encoder",
						     "vertical",    "optimization", "performance", "network", "appearance", "layout",
						     "security",    "diagnostics",  "updates",     "advanced", "about"};
	return ids;
}

SettingsDialog::SettingsDialog(UiHost &host, QWidget *parent) : QDialog(parent), host_(host)
{
	setObjectName(QStringLiteral("rdDialog"));
	setWindowTitle(uiText("Settings.Title", "RelayDock settings"));
	resize(900, 720);

	auto *layout = new QHBoxLayout(this);
	nav_ = new QListWidget(this);
	nav_->setObjectName(QStringLiteral("rdNav"));
	nav_->setFixedWidth(210);
	nav_->setAccessibleName(uiText("Settings.Nav.Name", "Settings pages"));
	stack_ = new QStackedWidget(this);
	layout->addWidget(nav_);
	layout->addWidget(stack_, 1);

	buildGeneral();
	buildPlatforms();
	buildStreaming();
	buildVideo();
	buildAudio();
	buildEncoder();
	buildVertical();
	buildOptimization();
	buildPerformance();
	buildNetwork();
	buildAppearance();
	buildLayout();
	buildSecurity();
	buildDiagnostics();
	buildUpdates();
	buildAdvanced();
	buildAbout();

	connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row < 0)
			return;
		stack_->setCurrentIndex(row);
		refreshCurrent();
	});
	// Queued, so a control never gets refreshed in the middle of its own signal.
	connect(&host_.app(), &AppContext::configChanged, this, &SettingsDialog::refreshCurrent, Qt::QueuedConnection);
	connect(&host_.app().performance(), &PerformanceMonitor::snapshotUpdated, this, [this] {
		if (!isVisible())
			return;
		const int row = stack_->currentIndex();
		if (row >= 0 && (pages_[static_cast<size_t>(row)].id == "performance" || pages_[static_cast<size_t>(row)].id == "network"))
			refreshCurrent();
	});
	connect(&host_.theme(), &Theme::changed, this, [this] {
		// Icons are drawn in the theme's text colour.
		for (int row = 0; row < nav_->count(); ++row) {
			const QByteArray icon = nav_->item(row)->data(Qt::UserRole).toByteArray();
			nav_->item(row)->setIcon(host_.theme().icon(icon.constData()));
		}
	});

	host_.theme().attach(this);
	nav_->setCurrentRow(0);
}

void SettingsDialog::showPage(const std::string &id)
{
	for (size_t i = 0; i < pages_.size(); ++i) {
		if (pages_[i].id == id) {
			nav_->setCurrentRow(static_cast<int>(i));
			return;
		}
	}
}

SettingsDialog::PageBuilder SettingsDialog::beginPage(const QString &title, const QString &intro)
{
	PageBuilder builder;
	auto *scroll = new QScrollArea(stack_);
	scroll->setObjectName(QStringLiteral("rdScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto *content = new QWidget(scroll);
	content->setObjectName(QStringLiteral("rdPage"));
	builder.body = new QVBoxLayout(content);
	builder.body->setSpacing(10);
	builder.body->addWidget(makeLabel(title, "rdHeading", content));
	if (!intro.isEmpty()) {
		QLabel *label = makeLabel(intro, "rdMuted", content);
		label->setWordWrap(true);
		builder.body->addWidget(label);
	}
	scroll->setWidget(content);
	builder.page = scroll;
	return builder;
}

void SettingsDialog::addPage(const char *id, const QString &title, const char *icon, QWidget *page, std::function<void()> refresh)
{
	auto *item = new QListWidgetItem(host_.theme().icon(icon), title, nav_);
	item->setData(Qt::UserRole, QByteArray(icon));
	stack_->addWidget(page);
	pages_.push_back({id, page, std::move(refresh)});
}

void SettingsDialog::refreshCurrent()
{
	const int row = stack_->currentIndex();
	if (row < 0 || row >= static_cast<int>(pages_.size()) || refreshing_)
		return;
	if (!pages_[static_cast<size_t>(row)].refresh)
		return;

	// A refresh sets every control to the saved value. Their change signals must stay quiet
	// while it runs, or a rounded display value would be written back as if the user typed it.
	std::vector<QSignalBlocker> blockers;
	const QList<QWidget *> children = pages_[static_cast<size_t>(row)].widget->findChildren<QWidget *>();
	blockers.reserve(static_cast<size_t>(children.size()));
	for (QWidget *child : children) {
		if (qobject_cast<QAbstractButton *>(child) || qobject_cast<QComboBox *>(child) ||
		    qobject_cast<QAbstractSpinBox *>(child) || qobject_cast<QAbstractSlider *>(child))
			blockers.emplace_back(child);
	}
	refreshing_ = true;
	pages_[static_cast<size_t>(row)].refresh();
	refreshing_ = false;
}

void SettingsDialog::commit()
{
	if (!refreshing_)
		host_.app().notifyConfigChanged();
}

QLabel *SettingsDialog::addNote(QVBoxLayout *layout, const QString &text)
{
	QLabel *label = makeLabel(text, "rdSmall");
	label->setWordWrap(true);
	label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(label);
	return label;
}

QLabel *SettingsDialog::addHeading(QVBoxLayout *layout, const QString &text)
{
	QLabel *label = makeLabel(text, "rdTitle");
	layout->addWidget(label);
	return label;
}

// ---- General ---------------------------------------------------------------------------------------

void SettingsDialog::buildGeneral()
{
	PageBuilder p = beginPage(uiText("Settings.General", "General"), QString());
	AppContext &app = host_.app();

	auto *confirmStop = new QCheckBox(uiText("General.ConfirmStopAll", "Ask before Stop All ends every stream"), p.page);
	auto *preflight = new QCheckBox(uiText("General.Preflight", "Run the preflight check before Start All Enabled"), p.page);
	auto *follow = new QCheckBox(uiText("General.FollowObs", "Start and stop with the OBS Start Streaming button"), p.page);
	p.body->addWidget(confirmStop);
	p.body->addWidget(preflight);
	addNote(p.body, uiText("General.Preflight.Note",
			       "The check runs in an instant. When everything is ready, your streams start without another click."));
	p.body->addWidget(follow);
	addNote(p.body, uiText("General.FollowObs.Note",
			       "With this on, RelayDock starts every enabled destination when you start the OBS stream and stops them when you stop it. If the OBS stream ends by itself, for example after an error, RelayDock's destinations keep running. OBS needs its own stream set up under OBS Settings, Stream for that button to work."));

	addHeading(p.body, uiText("General.Language", "Language"));
	addNote(p.body, uiText("General.Language.Note",
			       "RelayDock uses the language OBS runs in when a translation exists, and English otherwise."));
	p.body->addStretch(1);

	connect(confirmStop, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().general.confirmStopAll = on;
		commit();
	});
	connect(preflight, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().general.preflightOnStartAll = on;
		commit();
	});
	connect(follow, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().general.followObsStreaming = on;
		commit();
	});

	addPage("general", uiText("Settings.General", "General"), "sliders-horizontal", p.page, [=, &app] {
		confirmStop->setChecked(app.config().general.confirmStopAll);
		preflight->setChecked(app.config().general.preflightOnStartAll);
		follow->setChecked(app.config().general.followObsStreaming);
	});
}

// ---- Platforms -------------------------------------------------------------------------------------

void SettingsDialog::buildPlatforms()
{
	PageBuilder p = beginPage(uiText("Settings.Platforms", "Platforms"),
				  uiText("Platforms.Intro",
					 "What RelayDock knows about each platform. The limits come from the platform's own published pages on the date shown. Platforms change them without notice."));
	AppContext &app = host_.app();

	for (const IProvider *provider : app.providers().all()) {
		const ProviderInfo &info = provider->info();
		const ProviderLimits &limits = provider->limits();
		QFrame *panel = makePanel(p.page);
		auto *layout = new QVBoxLayout(panel);

		auto *header = new QHBoxLayout();
		auto *badge = new ProviderBadge(panel);
		badge->setProvider(qs(info.monogram), qs(info.accentColor));
		header->addWidget(badge);
		header->addWidget(makeLabel(qs(info.displayName), "rdTitle", panel), 1);
		auto *add = new QPushButton(uiText("Platforms.Add", "Add"), panel);
		const std::string id = info.id;
		connect(add, &QPushButton::clicked, this, [this, id] { host_.addDestination(id); });
		header->addWidget(add);
		layout->addLayout(header);

		QStringList facts;
		if (limits.maxVideoBitrateKbps > 0)
			facts << uiTextF("Platforms.VideoMax", "Video up to {0} Kbps.", limits.maxVideoBitrateKbps);
		else if (limits.recommendedVideoBitrateKbps > 0)
			facts << uiTextF("Platforms.VideoRecommended", "No published video limit. Guides suggest about {0} Kbps.",
					 limits.recommendedVideoBitrateKbps);
		else
			facts << uiText("Platforms.VideoNone", "No published video bitrate limit.");
		if (limits.maxLongEdge > 0 && limits.maxShortEdge > 0)
			facts << uiTextF("Platforms.SizeMax", "Up to {0}x{1}.", limits.maxLongEdge, limits.maxShortEdge);
		if (limits.maxFps > 0)
			facts << uiTextF("Platforms.FpsMax", "Up to {0} FPS.", limits.maxFps);
		if (limits.maxAudioBitrateKbps > 0)
			facts << uiTextF("Platforms.AudioMax", "Audio up to {0} Kbps.", limits.maxAudioBitrateKbps);
		if (limits.keyframeIntervalSec > 0)
			facts << uiTextF("Platforms.Keyframe", "Keyframe every {0} s.", limits.keyframeIntervalSec);
		facts << uiTextF("Platforms.Codecs", "Video codecs: {0}.", join(limits.videoCodecs, ", "));
		facts << (info.tlsRequired ? uiText("Platforms.TlsRequired", "Encrypted RTMPS only.")
					   : info.userSuppliesServer ? uiText("Platforms.UserServer", "You enter the server address.")
								     : uiText("Platforms.TlsDefault", "Encrypted RTMPS by default."));
		if (!limits.verticalSupported)
			facts << uiText("Platforms.NoVertical", "Horizontal video only.");
		facts << (info.testSupport == TestSupport::PrivateStream
				  ? uiText("Platforms.TestStream", "Supports a test stream that viewers do not see.")
				  : uiText("Platforms.TestReach", "Test connection checks that the server answers."));
		addNote(layout, facts.join(QStringLiteral(" ")));

		if (!limits.checkedOn.empty()) {
			auto *row = new QHBoxLayout();
			row->addWidget(makeLabel(uiTextF("Platforms.Checked", "Checked on {0}.", limits.checkedOn), "rdSmall", panel));
			row->addStretch(1);
			if (limits.source.rfind("https://", 0) == 0) {
				auto *source = new QPushButton(uiText("Platforms.Source", "Open source page"), panel);
				source->setToolTip(qs(limits.source));
				const QString url = qs(limits.source);
				connect(source, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
				row->addWidget(source);
			}
			layout->addLayout(row);
		}
		p.body->addWidget(panel);
	}
	p.body->addStretch(1);
	addPage("platforms", uiText("Settings.Platforms", "Platforms"), "layers", p.page, nullptr);
}

// ---- Streaming -------------------------------------------------------------------------------------

void SettingsDialog::buildStreaming()
{
	PageBuilder p = beginPage(uiText("Settings.Streaming", "Streaming"),
				  uiText("Streaming.Intro",
					 "How your enabled destinations will be encoded. Destinations whose settings match exactly share one video encoder, which saves processing. Every destination still needs its own upload."));
	AppContext &app = host_.app();

	QLabel *summary = makeLabel(QString(), "rdTitle", p.page);
	summary->setWordWrap(true);
	p.body->addWidget(summary);
	auto *groups = new QVBoxLayout();
	p.body->addLayout(groups);
	QLabel *hint = addNote(p.body, QString());
	p.body->addStretch(1);

	addPage("streaming", uiText("Settings.Streaming", "Streaming"), "radio", p.page, [=, &app] {
		clearLayout(groups);
		const std::vector<EffectiveDestination> effective = app.resolveEffective();
		const EncodePlan plan = planEncoders(effective);
		const StreamContext context = app.streamContext();

		if (plan.groups.empty()) {
			summary->setText(uiText("Streaming.None", "No destination is enabled."));
			hint->setText(QString());
			return;
		}
		summary->setText(plan.encodesSaved() > 0
					 ? uiTextF("Streaming.Summary.Shared", "Enabled destinations: {0}. Video encoders: {1}. Encodes saved by sharing: {2}.",
						   plan.destinationCount(), plan.videoEncoderCount(), plan.encodesSaved())
					 : uiTextF("Streaming.Summary", "Enabled destinations: {0}. Video encoders: {1}.", plan.destinationCount(),
						   plan.videoEncoderCount()));

		int number = 1;
		for (const EncoderGroup &group : plan.groups) {
			QFrame *panel = makePanel(p.page);
			auto *layout = new QVBoxLayout(panel);
			const VideoEncoderCaps *caps = context.findVideoEncoder(group.settings.encoderId);
			layout->addWidget(makeLabel(uiTextF("Streaming.Group", "Encoder {0}: {1}", number++,
							    caps ? caps->displayName : group.settings.encoderId),
						    "rdTitle", panel));
			layout->addWidget(makeLabel(
				uiTextF("Streaming.Group.Settings", "{0} at {1} FPS, {2}, {3}", formatResolution(group.settings.width, group.settings.height),
					static_cast<int>(std::lround(group.settings.fps)), formatBitrate(group.settings.bitrateKbps),
					group.settings.orientation == Orientation::Vertical ? loc("Card.Vertical", "vertical 9:16")
											    : loc("Card.Horizontal", "horizontal 16:9")),
				nullptr, panel));
			std::vector<std::string> names;
			for (const std::string &id : group.destinationIds) {
				const DestinationConfig *config = app.config().findDestination(id);
				names.push_back(config ? config->name : id);
			}
			QLabel *members = makeLabel(uiTextF("Streaming.Group.Members", "Feeds: {0}", joinNames(names)), "rdSmall", panel);
			members->setWordWrap(true);
			layout->addWidget(members);
			groups->addWidget(panel);
		}
		hint->setText(plan.videoEncoderCount() > 1 && app.config().performanceMode != PerformanceMode::Custom
				      ? uiText("Streaming.Hint.Split",
					       "Destinations end up on different encoders when a locked setting, the picture shape or a platform limit makes their settings differ.")
				      : QString());
	});
}

// ---- Video -----------------------------------------------------------------------------------------

void SettingsDialog::buildVideo()
{
	PageBuilder p = beginPage(uiText("Settings.Video", "Video"), QString());
	AppContext &app = host_.app();

	addHeading(p.body, uiText("Video.Obs", "OBS canvas"));
	QLabel *obsInfo = addNote(p.body, QString());
	addNote(p.body, uiText("Video.Obs.Note",
			       "Horizontal destinations take their picture from this canvas. Change it under OBS Settings, Video. A destination can send a smaller size or a lower frame rate, never a larger or faster one."));

	addHeading(p.body, uiText("Video.Vertical", "Vertical canvas"));
	auto *row = new QHBoxLayout();
	auto *size = new QComboBox(p.page);
	size->addItem(QStringLiteral("1080x1920"), QSize(1080, 1920));
	size->addItem(QStringLiteral("720x1280"), QSize(720, 1280));
	row->addWidget(makeLabel(uiText("Video.Vertical.Size", "Size"), nullptr, p.page));
	row->addWidget(size);
	row->addStretch(1);
	p.body->addLayout(row);
	QLabel *verticalNote = addNote(p.body, QString());
	addNote(p.body, uiText("Video.Vertical.Note",
			       "Vertical destinations take their picture from this canvas. It follows the OBS frame rate, and OBS only renders it while a vertical destination streams or the layout editor is open."));

	addHeading(p.body, uiText("Video.Scaling", "Scaling"));
	addNote(p.body, uiText("Video.Scaling.Note",
			       "When a destination sends a smaller size than its canvas, the graphics chip scales the picture with a bicubic filter. Potato Mode uses the cheaper bilinear filter. The aspect ratio always stays the same, so the picture is never stretched."));
	p.body->addStretch(1);

	connect(size, &QComboBox::activated, this, [=, this, &app](int index) {
		if (refreshing_)
			return;
		const QSize wanted = size->itemData(index).toSize();
		if (!app.vertical().setCanvasSize(wanted.width(), wanted.height())) {
			verticalNote->setText(uiText("Video.Vertical.Busy", "Stop your vertical destinations before you change the canvas size."));
			setStyleProperty(verticalNote, "rdTone", QStringLiteral("warning"));
			refreshCurrent();
			return;
		}
		app.config().verticalCanvas.width = wanted.width();
		app.config().verticalCanvas.height = wanted.height();
		obs_frontend_save(); // Layouts live in the scene collection
		commit();
	});

	addPage("video", uiText("Settings.Video", "Video"), "video", p.page, [=, &app] {
		const StreamContext context = app.streamContext();
		const CanvasInfo &h = context.horizontal;
		obsInfo->setText(uiTextF("Video.Obs.Info", "Canvas {0}, output {1}, {2} FPS.", formatResolution(h.baseWidth, h.baseHeight),
					 formatResolution(h.outputWidth, h.outputHeight), QString::number(h.fps(), 'f', 2).toStdString()));
		const QSize current(app.config().verticalCanvas.width, app.config().verticalCanvas.height);
		if (size->findData(current) < 0)
			size->addItem(qs(formatResolution(current.width(), current.height())), current);
		size->setCurrentIndex(size->findData(current));
		if (verticalNote->property("rdTone").toString().isEmpty())
			verticalNote->setText(QString());
	});
}

// ---- Audio -----------------------------------------------------------------------------------------

void SettingsDialog::buildAudio()
{
	PageBuilder p = beginPage(uiText("Settings.Audio", "Audio"), QString());
	AppContext &app = host_.app();

	addHeading(p.body, uiText("Audio.Obs", "OBS audio"));
	QLabel *info = addNote(p.body, QString());
	addNote(p.body, uiText("Audio.Obs.Note",
			       "RelayDock sends the audio OBS mixes. Change devices and levels in OBS itself. Each destination picks its own bitrate, encoder and audio track in its editor."));

	addHeading(p.body, uiText("Audio.Tracks", "Audio tracks"));
	addNote(p.body, uiText("Audio.Tracks.Note",
			       "OBS has six audio tracks. A destination sends one of them. Use this to keep copyrighted music off one platform: route the music to a track that destination does not send, in the OBS Advanced Audio Properties."));

	addHeading(p.body, uiText("Audio.Encoders", "Audio encoders on this PC"));
	QLabel *encoders = addNote(p.body, QString());
	p.body->addStretch(1);

	addPage("audio", uiText("Settings.Audio", "Audio"), "mic", p.page, [=, &app] {
		obs_audio_info audio{};
		if (obs_get_audio_info(&audio))
			info->setText(uiTextF("Audio.Obs.Info", "{0} Hz, channels: {1}.", audio.samples_per_sec,
					      static_cast<int>(get_audio_channels(audio.speakers))));
		else
			info->setText(uiText("Audio.Obs.None", "OBS audio is not running."));

		QStringList lines;
		const StreamContext context = app.streamContext();
		for (const AudioEncoderCaps &caps : context.audioEncoders)
			lines << uiTextF("Audio.Encoder.Line", "{0} ({1})", caps.displayName, caps.codec);
		encoders->setText(lines.isEmpty() ? uiText("Audio.Encoders.None", "OBS reports no audio encoder.") : lines.join(QStringLiteral("\n")));
	});
}

// ---- Encoder ---------------------------------------------------------------------------------------

void SettingsDialog::buildEncoder()
{
	PageBuilder p = beginPage(uiText("Settings.Encoder", "Encoder"),
				  uiText("Encoder.Intro",
					 "The video encoders OBS offers on this PC. A hardware encoder uses the graphics chip and leaves the processor to your game. Each destination picks its encoder in its editor."));
	AppContext &app = host_.app();

	QLabel *automatic = makeLabel(QString(), "rdTitle", p.page);
	automatic->setWordWrap(true);
	p.body->addWidget(automatic);
	auto *list = new QVBoxLayout();
	p.body->addLayout(list);
	auto *refresh = new QPushButton(uiText("Encoder.Refresh", "Look for encoders again"), p.page);
	refresh->setToolTip(uiText("Encoder.Refresh.Tip", "Use this after you installed or updated a graphics driver while OBS was open."));
	p.body->addWidget(refresh, 0, Qt::AlignLeft);
	p.body->addStretch(1);

	connect(refresh, &QPushButton::clicked, this, [this, &app] {
		app.encoders().refresh();
		refreshCurrent();
		commit();
	});

	addPage("encoder", uiText("Settings.Encoder", "Encoder"), "cpu", p.page, [=, &app] {
		clearLayout(list);
		const StreamContext context = app.streamContext();
		const std::string picked = pickVideoEncoder(context);
		const VideoEncoderCaps *pickedCaps = context.findVideoEncoder(picked);
		automatic->setText(pickedCaps ? uiTextF("Encoder.Automatic", "Automatic picks {0}.", pickedCaps->displayName)
					      : uiText("Encoder.Automatic.None", "OBS offers no encoder the RTMP output accepts."));

		for (const VideoEncoderCaps &caps : context.videoEncoders) {
			QFrame *panel = makePanel(p.page);
			auto *layout = new QVBoxLayout(panel);
			layout->addWidget(makeLabel(qs(caps.displayName), "rdTitle", panel));
			QStringList facts;
			facts << uiTextF("Encoder.Codec", "Codec {0}.", caps.codec);
			facts << (caps.hardware ? uiText("Encoder.Hardware", "Runs on the graphics chip.")
						: uiText("Encoder.Software", "Runs on the processor."));
			facts << (caps.dynamicBitrate ? uiText("Encoder.Dynamic", "Bitrate can change while live.")
						      : uiText("Encoder.Static", "A bitrate change needs a reconnect."));
			if (!caps.presets.empty())
				facts << uiTextF("Encoder.Presets", "Presets: {0}.", join(caps.presets, ", "));
			if (std::find(context.outputVideoCodecs.begin(), context.outputVideoCodecs.end(), caps.codec) ==
			    context.outputVideoCodecs.end())
				facts << uiText("Encoder.NotAccepted", "The RTMP output does not accept this codec.");
			QLabel *line = makeLabel(facts.join(QStringLiteral(" ")), "rdSmall", panel);
			line->setWordWrap(true);
			layout->addWidget(line);
			list->addWidget(panel);
		}
	});
}

// ---- Vertical layout -------------------------------------------------------------------------------

void SettingsDialog::buildVertical()
{
	PageBuilder p = beginPage(uiText("Settings.Vertical", "Vertical Layout"),
				  uiText("Vertical.Intro",
					 "A vertical layout arranges your sources on the 9:16 canvas. Layouts belong to the OBS scene collection and are saved with it. A new layout shows your whole OBS picture, with empty space above and below. To zoom in and crop the sides instead, open the editor and set Scaling to Fill."));
	AppContext &app = host_.app();

	auto *list = new QListWidget(p.page);
	list->setAccessibleName(uiText("Vertical.List.Name", "Vertical layouts"));
	list->setMinimumHeight(140);
	p.body->addWidget(list);

	auto *buttons = new QHBoxLayout();
	auto *edit = new QPushButton(uiText("Vertical.Edit", "Open editor..."), p.page);
	edit->setObjectName(QStringLiteral("rdPrimary"));
	auto *add = new QPushButton(uiText("Vertical.Add", "Add"), p.page);
	auto *duplicate = new QPushButton(uiText("Vertical.Duplicate", "Duplicate"), p.page);
	auto *rename = new QPushButton(uiText("Vertical.Rename", "Rename..."), p.page);
	auto *remove = new QPushButton(uiText("Vertical.Delete", "Delete..."), p.page);
	for (QPushButton *button : {edit, add, duplicate, rename, remove})
		buttons->addWidget(button);
	buttons->addStretch(1);
	p.body->addLayout(buttons);
	QLabel *usage = addNote(p.body, QString());
	p.body->addStretch(1);

	auto selectedId = [list] { return list->currentItem() ? ss(list->currentItem()->data(Qt::UserRole).toString()) : std::string(); };
	auto store = [this, &app](std::vector<VerticalLayout> layouts) {
		app.vertical().setLayouts(std::move(layouts));
		obs_frontend_save(); // Layouts live in the scene collection
		refreshCurrent();
	};

	connect(edit, &QPushButton::clicked, this, [=, this] {
		host_.openVerticalEditor(selectedId());
		refreshCurrent();
	});
	connect(list, &QListWidget::itemDoubleClicked, this, [=, this] {
		host_.openVerticalEditor(selectedId());
		refreshCurrent();
	});
	connect(add, &QPushButton::clicked, this, [=, &app] {
		std::vector<VerticalLayout> layouts = app.vertical().layouts();
		VerticalLayout layout = makeDefaultVerticalLayout(app.vertical().canvasWidth(), app.vertical().canvasHeight());
		layout.id = generateUuid();
		layout.name = locf("Vertical.NewName", "Layout {0}", layouts.size() + 1);
		layouts.push_back(std::move(layout));
		store(std::move(layouts));
	});
	connect(duplicate, &QPushButton::clicked, this, [=, &app] {
		std::vector<VerticalLayout> layouts = app.vertical().layouts();
		const std::string id = selectedId();
		for (const VerticalLayout &source : app.vertical().layouts()) {
			if (source.id != id)
				continue;
			VerticalLayout copy = source;
			copy.id = generateUuid();
			copy.name = locf("Destination.CopyName", "{0} copy", source.name);
			for (LayoutItem &item : copy.items)
				item.id = generateUuid();
			layouts.push_back(std::move(copy));
			store(std::move(layouts));
			return;
		}
	});
	connect(rename, &QPushButton::clicked, this, [=, this, &app] {
		std::vector<VerticalLayout> layouts = app.vertical().layouts();
		const std::string id = selectedId();
		for (VerticalLayout &layout : layouts) {
			if (layout.id != id)
				continue;
			bool ok = false;
			const QString name = QInputDialog::getText(this, uiText("Vertical.Rename.Title", "Rename layout"),
								   uiText("Vertical.Rename.Label", "Name"), QLineEdit::Normal, qs(layout.name), &ok)
						     .trimmed();
			if (!ok || name.isEmpty())
				return;
			layout.name = ss(name.left(64));
			store(std::move(layouts));
			return;
		}
	});
	connect(remove, &QPushButton::clicked, this, [=, this, &app] {
		std::vector<VerticalLayout> layouts = app.vertical().layouts();
		const std::string id = selectedId();
		const auto found = std::find_if(layouts.begin(), layouts.end(), [&](const VerticalLayout &l) { return l.id == id; });
		if (found == layouts.end() || layouts.size() <= 1)
			return;
		QMessageBox box(QMessageBox::Question, uiText("Vertical.Delete.Title", "Delete layout"),
				uiTextF("Vertical.Delete.Question", "Delete the layout {0}?", found->name), QMessageBox::Yes | QMessageBox::Cancel,
				this);
		box.setInformativeText(uiText("Vertical.Delete.Detail", "Destinations that used it switch to your first layout."));
		box.setDefaultButton(QMessageBox::Cancel);
		if (box.exec() != QMessageBox::Yes)
			return;
		layouts.erase(found);
		store(std::move(layouts));
	});

	addPage("vertical", uiText("Settings.Vertical", "Vertical Layout"), "smartphone", p.page, [=, &app] {
		const QString previous = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
		list->clear();
		for (const VerticalLayout &layout : app.vertical().layouts()) {
			auto *item = new QListWidgetItem(uiTextF("Vertical.Item", "{0} (items: {1})", layout.name, layout.items.size()), list);
			item->setData(Qt::UserRole, qs(layout.id));
			if (qs(layout.id) == previous)
				list->setCurrentItem(item);
		}
		if (!list->currentItem() && list->count() > 0)
			list->setCurrentRow(0);
		remove->setEnabled(list->count() > 1);

		std::vector<std::string> users;
		for (const DestinationConfig &destination : app.config().destinations) {
			if (destination.video.orientation == Orientation::Vertical)
				users.push_back(destination.name);
		}
		usage->setText(users.empty() ? uiText("Vertical.Usage.None", "No destination is set to vertical yet. Choose Vertical as the shape in a destination's Video settings.")
					     : uiTextF("Vertical.Usage", "Vertical destinations: {0}.", joinNames(users)));
	});
}

// ---- Automatic optimisation ------------------------------------------------------------------------

void SettingsDialog::buildOptimization()
{
	PageBuilder p = beginPage(uiText("Settings.Optimization", "Automatic Optimization"),
				  uiText("Optimization.Intro",
					 "RelayDock watches dropped frames, encoder lag and rendering lag while you stream. When one stays high, it can lower bitrate, frame rate or resolution, and bring them back when the problem is gone. These are fixed rules, not artificial intelligence."));
	AppContext &app = host_.app();

	auto *group = new QButtonGroup(p.page);
	auto *off = new QRadioButton(uiText("Optimization.Off", "Off"), p.page);
	auto *suggest = new QRadioButton(uiText("Optimization.Suggest", "Suggest changes"), p.page);
	auto *automatic = new QRadioButton(uiText("Optimization.Automatic", "Automatic"), p.page);
	group->addButton(off, 0);
	group->addButton(suggest, 1);
	group->addButton(automatic, 2);
	p.body->addWidget(off);
	addNote(p.body, uiText("Optimization.Off.Note", "RelayDock changes nothing and proposes nothing."));
	p.body->addWidget(suggest);
	addNote(p.body, uiText("Optimization.Suggest.Note",
			       "A suggestion appears in the dock. You choose Apply, Ignore or Lock Setting. Nothing changes until you do."));
	p.body->addWidget(automatic);
	addNote(p.body, uiText("Optimization.Automatic.Note",
			       "RelayDock applies changes that need no reconnect, such as a lower bitrate, and tells you in the dock."));

	auto *reconnecting = new QCheckBox(uiText("Optimization.AllowReconnect", "In Automatic mode, also apply changes that reconnect a destination"), p.page);
	p.body->addWidget(reconnecting);
	addNote(p.body, uiText("Optimization.AllowReconnect.Note",
			       "A lower frame rate or resolution needs a new encoder, which interrupts that destination for a moment. With this off, those changes stay suggestions."));

	addHeading(p.body, uiText("Optimization.Rules", "How it decides"));
	addNote(p.body, uiText("Optimization.Rules.Text",
			       "A measurement has to stay above its limit for 20 seconds before anything happens. After a change, RelayDock waits 60 seconds. Quality returns one step at a time after five minutes without a problem. If a step up brings the problem back, the next attempt waits twice as long. A setting you locked is never changed, and a destination can opt out in its Advanced settings."));

	addHeading(p.body, uiText("Optimization.History", "Changes in this OBS session"));
	QLabel *history = addNote(p.body, QString());
	p.body->addStretch(1);

	connect(group, &QButtonGroup::idClicked, this, [this, &app](int id) {
		app.config().optimizer.mode = id == 0 ? OptimizationMode::Off : id == 1 ? OptimizationMode::Suggest : OptimizationMode::Automatic;
		commit();
	});
	connect(reconnecting, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().optimizer.allowReconnectingChanges = on;
		commit();
	});

	addPage("optimization", uiText("Settings.Optimization", "Automatic Optimization"), "zap", p.page, [=, &app] {
		const OptimizerConfig &config = app.config().optimizer;
		off->setChecked(config.mode == OptimizationMode::Off);
		suggest->setChecked(config.mode == OptimizationMode::Suggest);
		automatic->setChecked(config.mode == OptimizationMode::Automatic);
		reconnecting->setChecked(config.allowReconnectingChanges);
		reconnecting->setEnabled(config.mode == OptimizationMode::Automatic);

		QStringList lines;
		for (const AppliedChange &change : app.performance().history())
			lines << QStringLiteral("%1  %2  %3")
					 .arg(qs(change.timeUtc), change.automatic ? uiText("Optimization.History.Auto", "automatic")
										   : uiText("Optimization.History.You", "accepted by you"),
					      qs(change.text));
		history->setText(lines.isEmpty() ? uiText("Optimization.History.None", "None.") : lines.join(QStringLiteral("\n")));
	});
}

// ---- Performance -----------------------------------------------------------------------------------

void SettingsDialog::buildPerformance()
{
	PageBuilder p = beginPage(uiText("Settings.Performance", "Performance"),
				  uiText("Performance.Intro",
					 "The performance mode picks resolution, frame rate, bitrate and encoder for every setting you have not locked."));
	AppContext &app = host_.app();

	auto *group = new QButtonGroup(p.page);
	struct ModeRow {
		PerformanceMode mode;
		QString title;
		QString note;
	};
	const ModeRow rows[] = {
		{PerformanceMode::Potato, uiText("Mode.Potato", "Potato"),
		 uiText("Performance.Potato.Note",
			"For a weak PC or a demanding game. 720p at 30 FPS, the fastest encoder preset, one shared encoder, slower measuring and no interface animations.")},
		{PerformanceMode::Balanced, uiText("Mode.Balanced", "Balanced"),
		 uiText("Performance.Balanced.Note",
			"Up to 1080p at 60 FPS. Every destination on the same canvas gets the same settings, so they share one encoder.")},
		{PerformanceMode::Quality, uiText("Mode.Quality", "Quality"),
		 uiText("Performance.Quality.Note",
			"Each destination uses its platform's own limit. Destinations with different limits need their own encoder, which costs more.")},
		{PerformanceMode::Custom, uiText("Mode.Custom", "Custom"),
		 uiText("Performance.Custom.Note", "Every destination uses exactly the values you saved in its editor.")},
	};
	std::vector<QRadioButton *> radios;
	for (const ModeRow &row : rows) {
		auto *radio = new QRadioButton(row.title, p.page);
		group->addButton(radio, static_cast<int>(row.mode));
		radios.push_back(radio);
		p.body->addWidget(radio);
		addNote(p.body, row.note);
	}

	auto *preventLag = new QCheckBox(uiText("Dock.PreventLag", "Prevent Game Lag"), p.page);
	p.body->addWidget(preventLag);
	addNote(p.body, uiText("Performance.PreventLag.Note",
			       "Automatic optimisation reacts after 12 seconds instead of 20, at half the usual limits, and also watches processor load. It waits ten minutes instead of five before it raises quality again. It needs automatic optimisation set to Suggest or Automatic."));

	addHeading(p.body, uiText("Performance.Now", "Right now"));
	QLabel *readings = addNote(p.body, QString());
	p.body->addStretch(1);

	connect(group, &QButtonGroup::idClicked, this, [this, &app](int id) {
		if (refreshing_)
			return;
		app.config().performanceMode = static_cast<PerformanceMode>(id);
		commit();
		// Live destinations take a new bitrate at once. Anything else waits for their next start.
		std::vector<std::string> active;
		for (const DestinationConfig &destination : app.config().destinations) {
			if (app.outputs().runtime(destination.id).active())
				active.push_back(destination.id);
		}
		app.outputs().applyEffectiveChanges(active, false);
	});
	connect(preventLag, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().optimizer.preventGameLag = on;
		commit();
	});

	addPage("performance", uiText("Settings.Performance", "Performance"), "gauge", p.page, [=, &app] {
		for (QRadioButton *radio : radios)
			radio->setChecked(group->id(radio) == static_cast<int>(app.config().performanceMode));
		preventLag->setChecked(app.config().optimizer.preventGameLag);

		const PerformanceSnapshot &s = app.performance().snapshot();
		QStringList lines;
		lines << uiTextF("Performance.Line.Cpu", "Processor: OBS {0}, whole PC {1}", ss(percentText(s.obsCpuPercent, 0)),
				 ss(percentText(s.systemCpuPercent, 0)));
		lines << uiTextF("Performance.Line.Gpu", "Graphics: {0}", ss(percentText(s.gpuPercent, 0)));
		lines << uiTextF("Performance.Line.Memory", "OBS memory: {0} MB", static_cast<int>(std::max(0.0, s.memoryMb)));
		lines << uiTextF("Performance.Line.Lag", "Rendering lag {0}, encoder lag {1}, dropped frames {2}", ss(percentText(s.renderLagPercent, 1)),
				 ss(percentText(s.encodeLagPercent, 1)), ss(percentText(s.worstDropPercent, 1)));
		lines << uiTextF("Performance.Line.Encoders", "Video encoders: {0}. Live destinations: {1}.", s.videoEncoders, s.liveDestinations);
		readings->setText(lines.join(QStringLiteral("\n")));
	});
}

} // namespace rd
