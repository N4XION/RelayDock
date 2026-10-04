// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/destination_dialog.h"

#include "app/app_context.h"
#include "app/background_tasks.h"
#include "encoders/video_math.h"
#include "network/stream_url.h"
#include "outputs/output_manager.h"
#include "outputs/vertical_canvas.h"
#include "providers/provider_base.h"
#include "ui/key_clipboard.h"
#include "utils/strings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace rd {

namespace {

bool contains(const std::vector<std::string> &list, const std::string &value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

// A value control with a Lock box beside it.
QWidget *withLock(QWidget *control, QCheckBox *lock, QWidget *parent)
{
	auto *row = new QWidget(parent);
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(control, 1);
	layout->addWidget(lock);
	return row;
}

QCheckBox *makeLock(QWidget *parent)
{
	auto *lock = new QCheckBox(uiText("Editor.Lock", "Lock"), parent);
	lock->setToolTip(uiText("Editor.Lock.Tip",
				"Locked: RelayDock always uses your value. Unlocked: the performance mode and automatic optimisation may pick another value."));
	return lock;
}

void selectData(QComboBox *combo, const QVariant &value, int fallbackIndex = 0)
{
	const int index = combo->findData(value);
	combo->setCurrentIndex(index >= 0 ? index : fallbackIndex);
}

} // namespace

// ---- SecretField -----------------------------------------------------------------------------------

SecretField::SecretField(UiHost &host, const QString &what, QWidget *parent) : QWidget(parent), host_(host), what_(what)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);

	auto *row = new QHBoxLayout();
	mask_ = new QLabel(qs(secretMask()), this);
	mask_->setAccessibleName(uiTextF("Secret.Saved.Name", "{0}, saved and hidden", ss(what_)));
	entry_ = new QLineEdit(this);
	entry_->setEchoMode(QLineEdit::Password);
	entry_->setPlaceholderText(uiTextF("Secret.Placeholder", "Paste your {0}", ss(what_)));
	entry_->setAccessibleName(what_);
	entry_->setMaxLength(2048);
	// Keep the secret out of features that remember or reshape text.
	entry_->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase | Qt::ImhSensitiveData);
	entry_->setContextMenuPolicy(Qt::DefaultContextMenu);

	replace_ = new QPushButton(uiText("Secret.Replace", "Replace"), this);
	copy_ = new QPushButton(uiText("Secret.Copy", "Copy"), this);
	copy_->setToolTip(uiText("Secret.Copy.Tip",
				 "Puts it on the clipboard for 30 seconds. Other programs can read the clipboard during that time."));
	remove_ = new QPushButton(uiText("Secret.Remove", "Remove"), this);
	undo_ = new QPushButton(this);

	row->addWidget(mask_);
	row->addWidget(entry_, 1);
	row->addStretch(0);
	row->addWidget(replace_);
	row->addWidget(copy_);
	row->addWidget(remove_);
	row->addWidget(undo_);
	layout->addLayout(row);

	note_ = makeLabel(QString(), "rdSmall", this);
	note_->setWordWrap(true);
	layout->addWidget(note_);

	connect(replace_, &QPushButton::clicked, this, [this] {
		entering_ = true;
		showState();
		entry_->setFocus();
		Q_EMIT changed();
	});
	connect(remove_, &QPushButton::clicked, this, [this] {
		removing_ = true;
		showState();
		Q_EMIT changed();
	});
	connect(undo_, &QPushButton::clicked, this, [this] {
		removing_ = false;
		entering_ = false;
		entry_->clear();
		showState();
		Q_EMIT changed();
	});
	connect(copy_, &QPushButton::clicked, this, [this] {
		SecretString secret;
		if (fetch && fetch(secret) && host_.clipboard().copy(secret))
			note_->setText(uiText("Secret.Copied", "Copied. RelayDock clears the clipboard after 30 seconds."));
		else
			note_->setText(uiText("Secret.CopyFailed", "Windows did not accept the clipboard data. Nothing was copied."));
	});
	connect(entry_, &QLineEdit::textChanged, this, [this] { Q_EMIT changed(); });
	showState();
}

void SecretField::setSaved(bool saved)
{
	saved_ = saved;
	removing_ = false;
	entering_ = false;
	entry_->clear();
	showState();
}

void SecretField::showState()
{
	const bool showSaved = saved_ && !removing_ && !entering_;
	const bool showEntry = !removing_ && (!saved_ || entering_);
	mask_->setVisible(showSaved);
	replace_->setVisible(showSaved);
	copy_->setVisible(showSaved);
	remove_->setVisible(showSaved);
	entry_->setVisible(showEntry);
	undo_->setVisible(removing_ || (saved_ && entering_));

	if (removing_) {
		undo_->setText(uiText("Secret.Undo", "Undo"));
		note_->setText(uiTextF("Secret.Removing", "The saved {0} is deleted when you save.", ss(what_)));
	} else if (showSaved) {
		note_->setText(uiText("Secret.SavedNote", "Saved in Windows Credential Manager. RelayDock never displays it."));
	} else if (saved_) {
		undo_->setText(uiText("Secret.KeepSaved", "Keep saved"));
		note_->setText(uiText("Secret.Replacing", "What you type replaces the saved value when you save."));
	} else {
		note_->setText(uiText("Secret.EntryNote", "RelayDock keeps it in Windows Credential Manager, never in a settings file."));
	}
}

SecretField::Action SecretField::action() const
{
	if (removing_)
		return Action::Remove;
	if ((entering_ || !saved_) && !entry_->text().trimmed().isEmpty())
		return Action::Replace;
	return Action::Keep;
}

SecretString SecretField::typed() const
{
	// Leading and trailing spaces or a line break come from copying, not from the key.
	return SecretString(entry_->text().trimmed().toStdString());
}

bool SecretField::willHaveSecret() const
{
	switch (action()) {
	case Action::Remove:
		return false;
	case Action::Replace:
		return true;
	case Action::Keep:
		return saved_;
	}
	return saved_;
}

// ---- DestinationDialog -----------------------------------------------------------------------------

DestinationDialog::DestinationDialog(UiHost &host, DestinationConfig config, bool isNew, QWidget *parent)
	: QDialog(parent), host_(host), working_(std::move(config)), isNew_(isNew)
{
	setObjectName(QStringLiteral("rdDialog"));
	provider_ = host_.app().providers().find(working_.provider);
	const QString providerName = provider_ ? qs(provider_->info().displayName) : qs(working_.provider);
	setWindowTitle(isNew_ ? uiTextF("Editor.Title.Add", "Add {0}", ss(providerName))
			      : uiTextF("Editor.Title.Edit", "Edit {0}", working_.name));
	setMinimumWidth(520);

	auto *layout = new QVBoxLayout(this);

	auto *header = new QHBoxLayout();
	auto *badge = new ProviderBadge(this);
	if (provider_)
		badge->setProvider(qs(provider_->info().monogram), qs(provider_->info().accentColor));
	header->addWidget(badge);
	header->addWidget(makeLabel(providerName, "rdHeading", this), 1);
	layout->addLayout(header);

	liveNote_ = new Banner(host_.theme(), this);
	liveNote_->setMessage("neutral", uiText("Editor.LiveNote", "This destination is live. Your changes apply the next time it starts."));
	liveNote_->setVisible(!isNew_ && host_.app().outputs().runtime(working_.id).active());
	layout->addWidget(liveNote_);

	tabs_ = new QTabWidget(this);
	tabs_->addTab(buildConnectionPage(), uiText("Editor.Tab.Connection", "Connection"));
	tabs_->addTab(buildVideoPage(), uiText("Editor.Tab.Video", "Video"));
	tabs_->addTab(buildAudioPage(), uiText("Editor.Tab.Audio", "Audio"));
	tabs_->addTab(buildAdvancedPage(), uiText("Editor.Tab.Advanced", "Advanced"));
	layout->addWidget(tabs_, 1);

	effective_ = makeLabel(QString(), "rdMuted", this);
	effective_->setWordWrap(true);
	layout->addWidget(effective_);

	feedback_ = new Banner(host_.theme(), this);
	feedback_->hide();
	layout->addWidget(feedback_);

	auto *buttons = new QHBoxLayout();
	if (provider_ && !provider_->info().keyHelpUrl.empty()) {
		auto *help = new QPushButton(uiText("Editor.KeyHelp", "Where is my stream key?"), this);
		help->setToolTip(qs(provider_->info().keyHelpUrl));
		const QString url = qs(provider_->info().keyHelpUrl);
		connect(help, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
		buttons->addWidget(help);
	}
	buttons->addStretch(1);
	auto *cancel = new QPushButton(uiText("Common.Cancel", "Cancel"), this);
	auto *save = new QPushButton(isNew_ ? uiText("Editor.Add", "Add") : uiText("Common.Save", "Save"), this);
	save->setObjectName(QStringLiteral("rdPrimary"));
	save->setDefault(true);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(save, &QPushButton::clicked, this, &DestinationDialog::accept);
	buttons->addWidget(cancel);
	buttons->addWidget(save);
	layout->addLayout(buttons);

	host_.theme().attach(this);
	load();
	loading_ = false;
	updateServerControls();
	updateEncoderControls();
	updateFeedback();
}

// ---- Pages ---------------------------------------------------------------------------------------

QWidget *DestinationDialog::buildConnectionPage()
{
	auto *page = new QWidget(this);
	auto *form = new QFormLayout(page);
	const ProviderInfo info = provider_ ? provider_->info() : ProviderInfo{};

	name_ = new QLineEdit(page);
	name_->setMaxLength(64);
	form->addRow(uiText("Editor.Name", "Name"), name_);

	server_ = new QComboBox(page);
	if (provider_) {
		for (const ServerOption &option : provider_->servers())
			server_->addItem(qs(option.name), qs(option.id));
		if (server_->count() > 0)
			server_->addItem(uiText("Editor.Server.Custom", "Custom address"), QString::fromUtf8(kCustomServerId));
	}
	if (server_->count() > 0)
		form->addRow(uiText("Editor.Server", "Server"), server_);
	else
		server_->hide();

	serverUrl_ = new QLineEdit(page);
	serverUrl_->setMaxLength(static_cast<int>(kMaxStreamUrlLength));
	serverUrl_->setPlaceholderText(info.tlsRequired ? QStringLiteral("rtmps://host:443/app") : QStringLiteral("rtmp://host/app"));
	serverUrl_->setToolTip(uiText("Editor.ServerUrl.Tip",
				      "The address the platform gives you. Put the stream key in its own field, never in this address."));
	serverUrlLabel_ = new QLabel(uiText("Editor.ServerUrl", "Server URL"), page);
	form->addRow(serverUrlLabel_, serverUrl_);

	key_ = new SecretField(host_, uiText("Editor.StreamKey.What", "stream key"), page);
	key_->fetch = [this](SecretString &out) { return host_.app().vault().get({working_.id, CredentialKind::StreamKey}, out).ok(); };
	form->addRow(uiText("Editor.StreamKey", "Stream key"), key_);

	useAuth_ = new QCheckBox(uiText("Editor.UseAuth", "The server asks for a user name and password"), page);
	username_ = new QLineEdit(page);
	username_->setMaxLength(256);
	usernameLabel_ = new QLabel(uiText("Editor.Username", "User name"), page);
	password_ = new SecretField(host_, uiText("Editor.Password.What", "password"), page);
	password_->fetch = [this](SecretString &out) { return host_.app().vault().get({working_.id, CredentialKind::Password}, out).ok(); };
	passwordLabel_ = new QLabel(uiText("Editor.Password", "Password"), page);
	if (info.supportsAuth) {
		form->addRow(QString(), useAuth_);
		form->addRow(usernameLabel_, username_);
		form->addRow(passwordLabel_, password_);
	} else {
		useAuth_->hide();
		username_->hide();
		usernameLabel_->hide();
		password_->hide();
		passwordLabel_->hide();
	}

	testButton_ = new QPushButton(uiText("Editor.Test", "Test connection"), page);
	testButton_->setToolTip(uiText("Editor.Test.Tip", "Checks that the server answers. It cannot check your stream key."));
	form->addRow(QString(), testButton_);
	testResult_ = new Banner(host_.theme(), page);
	testResult_->hide();
	form->addRow(testResult_);

	// What a streamer should know about this platform before going live.
	if (provider_) {
		QStringList notes;
		for (const std::string &note : provider_->setupNotes())
			notes << qs(note);
		if (!notes.isEmpty()) {
			auto *banner = new Banner(host_.theme(), page);
			banner->setMessage("neutral", notes.join(QStringLiteral("\n\n")));
			form->addRow(banner);
		}
	}

	auto changed = [this] {
		if (!loading_)
			updateFeedback();
	};
	connect(name_, &QLineEdit::textChanged, this, changed);
	connect(serverUrl_, &QLineEdit::textChanged, this, changed);
	connect(username_, &QLineEdit::textChanged, this, changed);
	connect(key_, &SecretField::changed, this, changed);
	connect(password_, &SecretField::changed, this, changed);
	connect(server_, &QComboBox::currentIndexChanged, this, [this] {
		if (loading_)
			return;
		updateServerControls();
		updateFeedback();
	});
	connect(useAuth_, &QCheckBox::toggled, this, [this] {
		if (loading_)
			return;
		updateServerControls();
		updateFeedback();
	});
	connect(testButton_, &QPushButton::clicked, this, &DestinationDialog::testConnection);
	return page;
}

QWidget *DestinationDialog::buildVideoPage()
{
	auto *page = new QWidget(this);
	videoForm_ = new QFormLayout(page);
	const StreamContext context = host_.app().streamContext();
	const ProviderLimits limits = provider_ ? provider_->limits() : ProviderLimits{};

	orientation_ = new QComboBox(page);
	orientation_->addItem(uiText("Editor.Horizontal", "Horizontal, 16:9 from the OBS canvas"), QStringLiteral("horizontal"));
	if (limits.verticalSupported || working_.video.orientation == Orientation::Vertical)
		orientation_->addItem(uiText("Editor.Vertical", "Vertical, 9:16 from the vertical canvas"), QStringLiteral("vertical"));
	videoForm_->addRow(uiText("Editor.Orientation", "Shape"), orientation_);

	layoutRow_ = new QWidget(page);
	{
		auto *row = new QHBoxLayout(layoutRow_);
		row->setContentsMargins(0, 0, 0, 0);
		layout_ = new QComboBox(layoutRow_);
		auto *edit = new QPushButton(uiText("Editor.Layout.Edit", "Edit layout..."), layoutRow_);
		row->addWidget(layout_, 1);
		row->addWidget(edit);
		connect(edit, &QPushButton::clicked, this, [this] {
			host_.openVerticalEditor(ss(layout_->currentData().toString()));
			// The editor may add, rename or delete layouts.
			const QString previous = layout_->currentData().toString();
			const QSignalBlocker blocker(layout_);
			layout_->clear();
			for (const VerticalLayout &layout : host_.app().vertical().layouts())
				layout_->addItem(qs(layout.name), qs(layout.id));
			selectData(layout_, previous);
		});
	}
	layoutLabel_ = new QLabel(uiText("Editor.Layout", "Vertical layout"), page);
	videoForm_->addRow(layoutLabel_, layoutRow_);

	resolution_ = new QComboBox(page);
	lockResolution_ = makeLock(page);
	videoForm_->addRow(uiText("Editor.Resolution", "Resolution"), withLock(resolution_, lockResolution_, page));

	fps_ = new QComboBox(page);
	lockFps_ = makeLock(page);
	videoForm_->addRow(uiText("Editor.Fps", "Frame rate"), withLock(fps_, lockFps_, page));

	bitrate_ = new QSpinBox(page);
	bitrate_->setRange(range::kMinVideoBitrateKbps, range::kMaxVideoBitrateKbps);
	bitrate_->setSingleStep(250);
	bitrate_->setSuffix(QStringLiteral(" Kbps"));
	if (limits.maxVideoBitrateKbps > 0)
		bitrate_->setToolTip(uiTextF("Editor.Bitrate.Limit", "{0} accepts up to {1} Kbps of video.",
					     provider_ ? provider_->info().displayName : std::string(), limits.maxVideoBitrateKbps));
	lockBitrate_ = makeLock(page);
	videoForm_->addRow(uiText("Editor.Bitrate", "Video bitrate"), withLock(bitrate_, lockBitrate_, page));

	encoder_ = new QComboBox(page);
	encoder_->addItem(uiText("Editor.Encoder.Auto", "Automatic"), QString::fromUtf8(kAutoEncoder));
	for (const VideoEncoderCaps &caps : context.videoEncoders) {
		// Only encoders whose codec both the RTMP output and the platform accept.
		if (!contains(context.outputVideoCodecs, caps.codec) || !contains(limits.videoCodecs, caps.codec))
			continue;
		encoder_->addItem(qs(caps.displayName), qs(caps.id));
	}
	encoder_->setToolTip(uiText("Editor.Encoder.Tip",
				    "Automatic picks a hardware encoder when your PC has one, otherwise x264. Destinations that use the same encoder and settings share one encode."));
	lockEncoder_ = makeLock(page);
	videoForm_->addRow(uiText("Editor.Encoder", "Encoder"), withLock(encoder_, lockEncoder_, page));

	rateControl_ = new QComboBox(page);
	videoForm_->addRow(uiText("Editor.RateControl", "Rate control"), rateControl_);
	preset_ = new QComboBox(page);
	videoForm_->addRow(uiText("Editor.Preset", "Preset"), preset_);
	profile_ = new QComboBox(page);
	videoForm_->addRow(uiText("Editor.Profile", "Profile"), profile_);

	keyframe_ = new QSpinBox(page);
	keyframe_->setRange(0, range::kMaxKeyframeIntervalSec);
	keyframe_->setSuffix(QStringLiteral(" s"));
	keyframe_->setSpecialValueText(uiText("Editor.Keyframe.Auto", "Encoder decides"));
	if (limits.keyframeIntervalSec > 0)
		keyframe_->setToolTip(uiTextF("Editor.Keyframe.Tip", "{0} recommends a keyframe every {1} seconds.",
					      provider_ ? provider_->info().displayName : std::string(), limits.keyframeIntervalSec));
	videoForm_->addRow(uiText("Editor.Keyframe", "Keyframe interval"), keyframe_);

	bFrames_ = new QSpinBox(page);
	bFrames_->setRange(-1, range::kMaxBFrames);
	bFrames_->setSpecialValueText(uiText("Editor.BFrames.Default", "Encoder default"));
	videoForm_->addRow(uiText("Editor.BFrames", "B-frames"), bFrames_);

	customOptions_ = new QLineEdit(page);
	customOptions_->setMaxLength(512);
	customOptions_->setPlaceholderText(QStringLiteral("name=value name=value"));
	videoForm_->addRow(uiText("Editor.CustomOptions", "Encoder options"), customOptions_);

	lockNote_ = makeLabel(QString(), "rdSmall", page);
	lockNote_->setWordWrap(true);
	videoForm_->addRow(lockNote_);

	auto changed = [this] {
		if (!loading_)
			updateFeedback();
	};
	auto encoderChanged = [this] {
		if (loading_)
			return;
		store();
		updateEncoderControls();
		updateFeedback();
	};
	connect(orientation_, &QComboBox::currentIndexChanged, this, [this] {
		if (loading_)
			return;
		store();
		load(); // Resolution and frame rate choices depend on the canvas
		updateEncoderControls();
		updateFeedback();
	});
	connect(encoder_, &QComboBox::currentIndexChanged, this, encoderChanged);
	for (QComboBox *combo : {layout_, resolution_, fps_, rateControl_, preset_, profile_})
		connect(combo, &QComboBox::currentIndexChanged, this, changed);
	for (QSpinBox *spin : {bitrate_, keyframe_, bFrames_})
		connect(spin, &QSpinBox::valueChanged, this, changed);
	for (QCheckBox *box : {lockResolution_, lockFps_, lockBitrate_})
		connect(box, &QCheckBox::toggled, this, changed);
	connect(lockEncoder_, &QCheckBox::toggled, this, encoderChanged);
	connect(customOptions_, &QLineEdit::textChanged, this, changed);
	return page;
}

QWidget *DestinationDialog::buildAudioPage()
{
	auto *page = new QWidget(this);
	auto *form = new QFormLayout(page);
	const StreamContext context = host_.app().streamContext();
	const ProviderLimits limits = provider_ ? provider_->limits() : ProviderLimits{};

	audioBitrate_ = new QComboBox(page);
	for (int kbps : {64, 96, 128, 160, 192, 256, 320}) {
		if (limits.maxAudioBitrateKbps > 0 && kbps > limits.maxAudioBitrateKbps && kbps != working_.audio.bitrateKbps)
			continue;
		audioBitrate_->addItem(QStringLiteral("%1 Kbps").arg(kbps), kbps);
	}
	if (audioBitrate_->findData(working_.audio.bitrateKbps) < 0)
		audioBitrate_->addItem(QStringLiteral("%1 Kbps").arg(working_.audio.bitrateKbps), working_.audio.bitrateKbps);
	if (limits.maxAudioBitrateKbps > 0)
		audioBitrate_->setToolTip(uiTextF("Editor.AudioBitrate.Limit", "{0} accepts up to {1} Kbps of audio.",
						  provider_ ? provider_->info().displayName : std::string(), limits.maxAudioBitrateKbps));
	form->addRow(uiText("Editor.AudioBitrate", "Audio bitrate"), audioBitrate_);

	audioEncoder_ = new QComboBox(page);
	audioEncoder_->addItem(uiText("Editor.Encoder.Auto", "Automatic"), QString::fromUtf8(kAutoEncoder));
	for (const AudioEncoderCaps &caps : context.audioEncoders) {
		if (contains(context.outputAudioCodecs, caps.codec))
			audioEncoder_->addItem(qs(caps.displayName), qs(caps.id));
	}
	form->addRow(uiText("Editor.AudioEncoder", "Audio encoder"), audioEncoder_);

	audioTrack_ = new QSpinBox(page);
	audioTrack_->setRange(1, range::kMaxAudioTrack);
	audioTrack_->setToolTip(uiText("Editor.AudioTrack.Tip",
				       "The OBS audio track this destination sends. Set up tracks in the OBS Advanced Audio Properties."));
	form->addRow(uiText("Editor.AudioTrack", "Audio track"), audioTrack_);

	auto changed = [this] {
		if (!loading_)
			updateFeedback();
	};
	connect(audioBitrate_, &QComboBox::currentIndexChanged, this, changed);
	connect(audioEncoder_, &QComboBox::currentIndexChanged, this, changed);
	connect(audioTrack_, &QSpinBox::valueChanged, this, changed);
	return page;
}

QWidget *DestinationDialog::buildAdvancedPage()
{
	auto *page = new QWidget(this);
	auto *form = new QFormLayout(page);

	autoReconnect_ = new QCheckBox(uiText("Editor.AutoReconnect", "Reconnect by itself when the connection drops"), page);
	form->addRow(QString(), autoReconnect_);

	reconnectAttempts_ = new QSpinBox(page);
	reconnectAttempts_->setRange(1, range::kMaxReconnectAttempts);
	form->addRow(uiText("Editor.ReconnectAttempts", "Attempts"), reconnectAttempts_);

	reconnectDelay_ = new QSpinBox(page);
	reconnectDelay_->setRange(1, range::kMaxReconnectDelaySec);
	reconnectDelay_->setSuffix(QStringLiteral(" s"));
	reconnectDelay_->setToolTip(uiText("Editor.ReconnectDelay.Tip",
					   "Wait before the first attempt. Each later wait is one and a half times longer, up to 15 minutes."));
	form->addRow(uiText("Editor.ReconnectDelay", "First wait"), reconnectDelay_);

	streamDelay_ = new QSpinBox(page);
	streamDelay_->setRange(0, range::kMaxStreamDelaySec);
	streamDelay_->setSuffix(QStringLiteral(" s"));
	streamDelay_->setSpecialValueText(uiText("Editor.StreamDelay.Off", "Off"));
	streamDelay_->setToolTip(uiText("Editor.StreamDelay.Tip",
					"Holds the stream back by this many seconds before sending it. Uses memory while it runs."));
	form->addRow(uiText("Editor.StreamDelay", "Stream delay"), streamDelay_);

	bindIp_ = new QLineEdit(page);
	bindIp_->setMaxLength(64);
	bindIp_->setPlaceholderText(uiText("Editor.BindIp.Default", "Default network adapter"));
	bindIp_->setToolTip(uiText("Editor.BindIp.Tip",
				   "Send this destination through the network adapter with this IP address. Leave empty unless you have more than one connection."));
	form->addRow(uiText("Editor.BindIp", "Bind to IP"), bindIp_);

	autoOptimize_ = new QCheckBox(uiText("Editor.AutoOptimize", "Let automatic optimisation adjust this destination"), page);
	autoOptimize_->setToolTip(uiText("Editor.AutoOptimize.Tip",
					 "Off: RelayDock never suggests or applies changes for this destination, whatever the global setting says."));
	form->addRow(QString(), autoOptimize_);

	auto changed = [this] {
		if (loading_)
			return;
		reconnectAttempts_->setEnabled(autoReconnect_->isChecked());
		reconnectDelay_->setEnabled(autoReconnect_->isChecked());
		updateFeedback();
	};
	connect(autoReconnect_, &QCheckBox::toggled, this, changed);
	connect(autoOptimize_, &QCheckBox::toggled, this, changed);
	for (QSpinBox *spin : {reconnectAttempts_, reconnectDelay_, streamDelay_})
		connect(spin, &QSpinBox::valueChanged, this, changed);
	connect(bindIp_, &QLineEdit::textChanged, this, changed);
	return page;
}

// ---- Loading and storing ---------------------------------------------------------------------------

void DestinationDialog::load()
{
	const bool wasLoading = loading_;
	loading_ = true;
	AppContext &app = host_.app();
	const StreamContext context = app.streamContext();

	name_->setText(qs(working_.name));
	if (server_->count() > 0)
		selectData(server_, qs(working_.serverId), 0);
	serverUrl_->setText(qs(working_.serverUrl));
	useAuth_->setChecked(working_.useAuth);
	username_->setText(qs(working_.username));
	if (wasLoading) {
		// First load only. Later reloads must not undo what the user typed into the fields.
		key_->setSaved(!isNew_ && app.vault().has({working_.id, CredentialKind::StreamKey}));
		password_->setSaved(!isNew_ && app.vault().has({working_.id, CredentialKind::Password}));
	}

	const bool vertical = working_.video.orientation == Orientation::Vertical;
	selectData(orientation_, vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal"));
	const CanvasInfo &canvas = vertical ? context.vertical : context.horizontal;

	layout_->clear();
	for (const VerticalLayout &layout : app.vertical().layouts())
		layout_->addItem(qs(layout.name), qs(layout.id));
	selectData(layout_, qs(app.vertical().resolveLayoutId(working_.verticalLayoutId)));

	resolution_->clear();
	resolution_->addItem(uiTextF("Editor.Resolution.Canvas", "Same as the canvas ({0})",
				     formatResolution(canvas.outputWidth, canvas.outputHeight)),
			     QSize(0, 0));
	for (const Size &size : resolutionChoices(canvas))
		resolution_->addItem(uiTextF("Editor.Resolution.Choice", "{0} ({1}p)", formatResolution(size.width, size.height), size.lines()),
				     QSize(size.width, size.height));
	const QSize savedSize(working_.video.width, working_.video.height);
	if (resolution_->findData(savedSize) < 0)
		resolution_->addItem(qs(formatResolution(working_.video.width, working_.video.height)), savedSize);
	selectData(resolution_, savedSize);

	fps_->clear();
	fps_->addItem(uiTextF("Editor.Fps.Obs", "Same as OBS ({0} FPS)", static_cast<int>(std::lround(canvas.fps()))), 0);
	for (int choice : fpsChoices(canvas))
		fps_->addItem(uiTextF("Editor.Fps.Choice", "{0} FPS", choice), choice);
	if (fps_->findData(working_.video.fps) < 0)
		fps_->addItem(uiTextF("Editor.Fps.Choice", "{0} FPS", working_.video.fps), working_.video.fps);
	selectData(fps_, working_.video.fps);

	bitrate_->setValue(working_.video.bitrateKbps);
	if (encoder_->findData(qs(working_.video.encoder)) < 0)
		encoder_->addItem(uiTextF("Editor.Encoder.Missing", "{0} (not available on this PC)", working_.video.encoder),
				  qs(working_.video.encoder));
	selectData(encoder_, qs(working_.video.encoder));
	keyframe_->setValue(working_.video.keyframeIntervalSec);
	bFrames_->setValue(working_.video.bFrames);
	customOptions_->setText(qs(working_.video.customOptions));

	lockResolution_->setChecked(working_.locks.resolution);
	lockFps_->setChecked(working_.locks.fps);
	lockBitrate_->setChecked(working_.locks.bitrate);
	lockEncoder_->setChecked(working_.locks.encoder);

	selectData(audioBitrate_, working_.audio.bitrateKbps);
	if (audioEncoder_->findData(qs(working_.audio.encoder)) < 0)
		audioEncoder_->addItem(qs(working_.audio.encoder), qs(working_.audio.encoder));
	selectData(audioEncoder_, qs(working_.audio.encoder));
	audioTrack_->setValue(working_.audio.track);

	autoReconnect_->setChecked(working_.connection.autoReconnect);
	reconnectAttempts_->setValue(working_.connection.reconnectAttempts);
	reconnectDelay_->setValue(working_.connection.reconnectDelaySec);
	reconnectAttempts_->setEnabled(working_.connection.autoReconnect);
	reconnectDelay_->setEnabled(working_.connection.autoReconnect);
	streamDelay_->setValue(working_.connection.streamDelaySec);
	bindIp_->setText(qs(working_.connection.bindIp));
	autoOptimize_->setChecked(working_.autoOptimize);

	loading_ = wasLoading;
}

void DestinationDialog::store()
{
	working_.name = trim(ss(name_->text()));
	if (server_->count() > 0)
		working_.serverId = ss(server_->currentData().toString());
	working_.serverUrl = trim(ss(serverUrl_->text()));
	working_.useAuth = provider_ && provider_->info().supportsAuth && useAuth_->isChecked();
	working_.username = trim(ss(username_->text()));

	working_.video.orientation = orientation_->currentData().toString() == QLatin1String("vertical") ? Orientation::Vertical
													   : Orientation::Horizontal;
	if (layout_->count() > 0)
		working_.verticalLayoutId = ss(layout_->currentData().toString());
	const QSize size = resolution_->currentData().toSize();
	working_.video.width = size.width();
	working_.video.height = size.height();
	working_.video.fps = fps_->currentData().toInt();
	working_.video.bitrateKbps = bitrate_->value();
	working_.video.encoder = ss(encoder_->currentData().toString());
	if (rateControl_->count() > 0)
		working_.video.rateControl = ss(rateControl_->currentData().toString());
	if (preset_->count() > 0)
		working_.video.preset = ss(preset_->currentData().toString());
	if (profile_->count() > 0)
		working_.video.profile = ss(profile_->currentData().toString());
	working_.video.keyframeIntervalSec = keyframe_->value();
	working_.video.bFrames = bFrames_->value();
	working_.video.customOptions = trim(ss(customOptions_->text()));

	working_.locks.resolution = lockResolution_->isChecked();
	working_.locks.fps = lockFps_->isChecked();
	working_.locks.bitrate = lockBitrate_->isChecked();
	working_.locks.encoder = lockEncoder_->isChecked();

	working_.audio.bitrateKbps = audioBitrate_->currentData().toInt();
	working_.audio.encoder = ss(audioEncoder_->currentData().toString());
	working_.audio.track = audioTrack_->value();

	working_.connection.autoReconnect = autoReconnect_->isChecked();
	working_.connection.reconnectAttempts = reconnectAttempts_->value();
	working_.connection.reconnectDelaySec = reconnectDelay_->value();
	working_.connection.streamDelaySec = streamDelay_->value();
	working_.connection.bindIp = trim(ss(bindIp_->text()));
	working_.autoOptimize = autoOptimize_->isChecked();
}

// ---- Dependent controls ----------------------------------------------------------------------------

void DestinationDialog::updateServerControls()
{
	const bool hasList = server_->count() > 0;
	const bool custom = !hasList || server_->currentData().toString() == QString::fromUtf8(kCustomServerId);
	serverUrl_->setVisible(custom);
	serverUrlLabel_->setVisible(custom);

	const bool auth = useAuth_->isChecked() && provider_ && provider_->info().supportsAuth;
	username_->setVisible(auth);
	usernameLabel_->setVisible(auth);
	password_->setVisible(auth);
	passwordLabel_->setVisible(auth);

	const bool vertical = orientation_->currentData().toString() == QLatin1String("vertical");
	layoutRow_->setVisible(vertical);
	layoutLabel_->setVisible(vertical);
}

const VideoEncoderCaps *DestinationDialog::currentCaps(const StreamContext &context) const
{
	// With "Automatic", the encoder is whatever the resolver picks for this destination.
	for (const EffectiveDestination &destination : host_.app().resolveEffectiveWith(working_)) {
		if (destination.id == working_.id)
			return context.findVideoEncoder(destination.video.encoderId);
	}
	return nullptr;
}

void DestinationDialog::updateEncoderControls()
{
	const bool wasLoading = loading_;
	loading_ = true;
	const StreamContext context = host_.app().streamContext();
	const VideoEncoderCaps *caps = currentCaps(context);

	rateControl_->clear();
	preset_->clear();
	profile_->clear();
	if (caps) {
		for (const std::string &value : caps->rateControls)
			rateControl_->addItem(qs(value), qs(value));
		selectData(rateControl_, qs(working_.video.rateControl), std::max(0, rateControl_->findData(QStringLiteral("CBR"))));

		if (!caps->presets.empty()) {
			preset_->addItem(uiText("Editor.Preset.Auto", "Automatic, from the performance mode"), QString());
			for (const std::string &value : caps->presets)
				preset_->addItem(qs(value), qs(value));
			selectData(preset_, qs(working_.video.preset));
		}
		bool anyProfile = false;
		for (const std::string &value : caps->profiles) {
			if (value.empty())
				continue;
			if (!anyProfile)
				profile_->addItem(uiText("Editor.Profile.Default", "Encoder default"), QString());
			anyProfile = true;
			profile_->addItem(qs(value), qs(value));
		}
		if (anyProfile)
			selectData(profile_, qs(working_.video.profile));

		if (caps->maxKeyframeIntervalSec > 0)
			keyframe_->setMaximum(std::min(range::kMaxKeyframeIntervalSec, caps->maxKeyframeIntervalSec));
		bFrames_->setMaximum(std::max(0, std::min(range::kMaxBFrames, caps->maxBFrames)));
	}

	// Only show what the encoder has.
	videoForm_->setRowVisible(rateControl_, caps && !caps->rateControls.empty());
	videoForm_->setRowVisible(preset_, caps && !caps->presets.empty());
	videoForm_->setRowVisible(profile_, profile_->count() > 0);
	videoForm_->setRowVisible(keyframe_, caps && caps->hasKeyframeInterval);
	videoForm_->setRowVisible(bFrames_, caps && !caps->bFramesKey.empty());
	videoForm_->setRowVisible(customOptions_, caps && !caps->optionsKey.empty());
	loading_ = wasLoading;
}

void DestinationDialog::updateFeedback()
{
	store();
	updateServerControls();
	AppContext &app = host_.app();

	// ---- What these settings lead to ---------------------------------------------------------
	const PerformanceMode mode = app.config().performanceMode;
	lockNote_->setText(mode == PerformanceMode::Custom
				   ? uiText("Editor.LockNote.Custom", "Custom mode uses every value exactly as you saved it.")
				   : uiTextF("Editor.LockNote.Mode",
					     "{0} mode picks resolution, frame rate, bitrate and encoder for settings you leave unlocked. Lock a setting to always use your value.",
					     ss(uiText(mode == PerformanceMode::Potato     ? "Mode.Potato"
						       : mode == PerformanceMode::Balanced ? "Mode.Balanced"
											   : "Mode.Quality",
						       mode == PerformanceMode::Potato     ? "Potato"
						       : mode == PerformanceMode::Balanced ? "Balanced"
											   : "Quality"))));

	QString summary;
	for (const EffectiveDestination &destination : app.resolveEffectiveWith(working_)) {
		if (destination.id != working_.id)
			continue;
		if (!destination.usable()) {
			summary = uiText("Editor.Effective.None", "OBS offers no encoder this destination can use.");
			break;
		}
		const StreamContext context = app.streamContext();
		const VideoEncoderCaps *caps = context.findVideoEncoder(destination.video.encoderId);
		summary = uiTextF("Editor.Effective", "Streams {0} at {1} FPS, {2} video and {3} Kbps audio, with {4}.",
				  formatResolution(destination.video.width, destination.video.height),
				  static_cast<int>(std::lround(destination.video.fps)), formatBitrate(destination.video.bitrateKbps),
				  destination.audio.bitrateKbps, caps ? caps->displayName : destination.video.encoderId);
		for (const std::string &note : destination.notes)
			summary += QStringLiteral("\n") + qs(note);
	}
	effective_->setText(summary);

	// ---- Problems ------------------------------------------------------------------------------
	if (!provider_) {
		feedback_->setMessage("error", uiText("Editor.NoProvider", "This RelayDock version does not know this platform."));
		feedback_->show();
		return;
	}
	const std::vector<ValidationIssue> issues =
		provider_->validate(working_, {key_->willHaveSecret(), password_->willHaveSecret()});
	QStringList lines;
	bool anyError = false;
	for (const ValidationIssue &issue : issues) {
		if (issue.severity == Severity::Error)
			anyError = true;
	}
	for (const ValidationIssue &issue : issues) {
		// Errors first. Warnings only when nothing blocks.
		if (anyError && issue.severity != Severity::Error)
			continue;
		if (lines.size() < 3)
			lines << qs(issue.message.text());
	}
	if (lines.isEmpty()) {
		feedback_->hide();
	} else {
		feedback_->setMessage(anyError ? "error" : "warning", lines.join(QStringLiteral("\n")));
		feedback_->show();
	}
}

// ---- Actions ---------------------------------------------------------------------------------------

void DestinationDialog::testConnection()
{
	store();
	if (!provider_)
		return;
	const Endpoint endpoint = provider_->endpoint(working_);
	const StreamUrlResult parsed = parseStreamUrl(endpoint.serverUrl);
	if (!parsed.ok()) {
		ReachResult invalid;
		invalid.status = ReachStatus::InvalidAddress;
		testResult_->setMessage("error", describeReachability(working_.name, invalid, false));
		testResult_->show();
		return;
	}

	if (!tester_) {
		tester_ = new ConnectionTester(this);
		connect(tester_, &ConnectionTester::finished, this, [this](const ReachResult &result) {
			const bool tls = provider_ && provider_->endpoint(working_).tls;
			testResult_->setMessage(result.status == ReachStatus::Reachable ? "ok" : "error",
						describeReachability(working_.name, result, tls));
			testButton_->setEnabled(true);
		});
	}
	testButton_->setEnabled(false);
	testResult_->setMessage("neutral", uiTextF("Card.Test.Running", "Testing the connection to {0}...", parsed.url.host));
	testResult_->show();
	tester_->start(parsed.url.host, parsed.url.effectivePort());
}

void DestinationDialog::accept()
{
	store();
	if (working_.name.empty()) {
		tabs_->setCurrentIndex(0);
		name_->setFocus();
		feedback_->setMessage("error", uiText("Editor.NameMissing", "Give this destination a name."));
		feedback_->show();
		return;
	}

	AppContext &app = host_.app();
	app.upsertDestination(working_);

	// Secrets go to Windows Credential Manager, never into the settings file.
	QStringList problems;
	auto commit = [&](SecretField *field, CredentialKind kind, const QString &what) {
		const CredentialId id{working_.id, kind};
		switch (field->action()) {
		case SecretField::Action::Keep:
			break;
		case SecretField::Action::Remove:
			app.vault().remove(id);
			break;
		case SecretField::Action::Replace: {
			const SecretString secret = field->typed();
			const SecretVault::SetResult result = app.vault().set(id, secret);
			if (result.keptForSessionOnly)
				problems << uiTextF("Editor.SessionOnly",
						    "Windows could not save the {0}. RelayDock keeps it in memory until OBS closes, so you can stream now. Enter it again after you restart OBS. Windows reported: {1}",
						    ss(what), result.result.detail);
			else if (!result.result.ok())
				problems << uiTextF("Editor.SecretFailed", "The {0} was not saved. {1}", ss(what), result.result.detail);
			break;
		}
		}
	};
	commit(key_, CredentialKind::StreamKey, uiText("Editor.StreamKey.What", "stream key"));
	if (provider_ && provider_->info().supportsAuth)
		commit(password_, CredentialKind::Password, uiText("Editor.Password.What", "password"));

	app.notifyConfigChanged();

	if (!problems.isEmpty()) {
		QMessageBox box(QMessageBox::Warning, uiText("Editor.SecretProblem.Title", "Saving the key"),
				problems.join(QStringLiteral("\n\n")), QMessageBox::Ok, this);
		box.exec();
	}
	QDialog::accept();
}

} // namespace rd
