// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// The second half of the settings pages: Network, Appearance, Layout, Security, Diagnostics,
// Updates, Advanced and About. The window itself and the other pages are in settings_dialog.cpp.
#include "ui/settings_dialog.h"

#include "app/app_context.h"
#include "app/background_tasks.h"
#include "app/diagnostics_service.h"
#include "app/performance_monitor.h"
#include "build_info.h"
#include "legal/legal_documents.h"
#include "outputs/output_manager.h"
#include "ui/key_clipboard.h"
#include "utils/log.h"
#include "utils/paths.h"
#include "utils/strings.h"
#include "utils/uuid.h"

#include <obs.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace rd {

namespace {

QFrame *makePanel(QWidget *parent)
{
	auto *panel = new QFrame(parent);
	panel->setObjectName(QStringLiteral("rdPanel"));
	return panel;
}

// A button that shows a colour and opens a picker.
void paintSwatch(QPushButton *button, const QString &hex, const QString &fallbackText)
{
	if (hex.isEmpty()) {
		button->setText(fallbackText);
		button->setStyleSheet(QString());
		return;
	}
	Rgb color;
	const bool valid = parseHexColor(hex.toStdString(), color);
	const QString text = valid ? toQColor(readableOn(color)).name() : QStringLiteral("#000000");
	button->setText(hex.toUpper());
	button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; color: %2; }").arg(hex, text));
}

QString sectionTitle(const std::string &id)
{
	if (id == section::kDestinations)
		return uiText("Dock.Section.Destinations", "Destinations");
	if (id == section::kSuggestions)
		return uiText("Dock.Section.Suggestions", "Suggestions");
	if (id == section::kPerformance)
		return uiText("Dock.Section.Performance", "Performance");
	if (id == section::kNetwork)
		return uiText("Dock.Section.Network", "Network");
	return qs(id);
}

} // namespace

// ---- Network ---------------------------------------------------------------------------------------

void SettingsDialog::buildNetwork()
{
	PageBuilder p = beginPage(uiText("Settings.Network", "Network"),
				  uiText("Network.Intro",
					 "Every destination needs its own upload. Tell RelayDock your upload speed and it warns you before you start more than your connection can carry. RelayDock runs no speed test and sends nothing to measure your line."));
	AppContext &app = host_.app();

	auto *form = new QFormLayout();
	auto *upload = new QDoubleSpinBox(p.page);
	upload->setRange(0.0, 10000.0);
	upload->setDecimals(1);
	upload->setSingleStep(1.0);
	upload->setSuffix(QStringLiteral(" Mbps"));
	upload->setSpecialValueText(uiText("Network.Upload.NotSet", "Not set"));
	upload->setToolTip(uiText("Network.Upload.Tip", "The upload speed from your internet plan or from a speed test you ran yourself."));
	form->addRow(uiText("Network.Upload", "Your upload speed"), upload);

	auto *safetyRow = new QHBoxLayout();
	auto *safety = new QSlider(Qt::Horizontal, p.page);
	safety->setRange(50, 95);
	safety->setSingleStep(5);
	safety->setPageStep(5);
	safety->setAccessibleName(uiText("Network.Safety.Name", "Share of the upload speed to use"));
	QLabel *safetyValue = makeLabel(QString(), "rdValue", p.page);
	safetyValue->setMinimumWidth(40);
	safetyRow->addWidget(safety, 1);
	safetyRow->addWidget(safetyValue);
	form->addRow(uiText("Network.Safety", "Use at most"), safetyRow);
	p.body->addLayout(form);
	addNote(p.body, uiText("Network.Safety.Note",
			       "Leave room for chat, voice and everything else on your network. 75 percent is a sound choice for most connections."));

	addHeading(p.body, uiText("Network.Budget", "Upload budget"));
	QLabel *summary = makeLabel(QString(), nullptr, p.page);
	summary->setWordWrap(true);
	p.body->addWidget(summary);
	auto *meter = new QProgressBar(p.page);
	meter->setObjectName(QStringLiteral("rdMeter"));
	meter->setTextVisible(false);
	meter->setRange(0, 100);
	p.body->addWidget(meter);
	QLabel *detail = addNote(p.body, QString());
	QLabel *perDestination = addNote(p.body, QString());
	p.body->addStretch(1);

	connect(upload, &QDoubleSpinBox::valueChanged, this, [this, &app](double mbps) {
		app.config().network.uploadKbps = static_cast<int>(std::lround(mbps * 1000.0));
		commit();
	});
	connect(safety, &QSlider::valueChanged, this, [this, &app, safetyValue](int percent) {
		safetyValue->setText(QStringLiteral("%1%").arg(percent));
		app.config().network.safetyPercent = percent;
		commit();
	});

	addPage("network", uiText("Settings.Network", "Network"), "network", p.page, [=, &app] {
		// Do not fight the user while they type or drag.
		if (!upload->hasFocus())
			upload->setValue(app.config().network.uploadKbps / 1000.0);
		if (!safety->isSliderDown())
			safety->setValue(app.config().network.safetyPercent);
		safetyValue->setText(QStringLiteral("%1%").arg(app.config().network.safetyPercent));

		const BandwidthBudget budget = currentBandwidth(app);
		if (budget.entries.empty()) {
			summary->setText(uiText("Dock.Network.None", "No destination is enabled."));
			meter->hide();
			detail->setText(QString());
			perDestination->setText(QString());
			return;
		}
		if (budget.status == BandwidthStatus::Unknown) {
			summary->setText(uiTextF("Dock.Network.Unknown", "Your destinations need {0} of upload.", formatBitrate(budget.requiredKbps)));
			meter->hide();
			detail->setText(uiText("Network.Unknown.Detail", "Enter your upload speed above to see whether that fits."));
		} else {
			summary->setText(uiTextF("Dock.Network.Known", "Needs {0} of your {1} upload", formatBitrate(budget.requiredKbps),
						 formatBitrate(budget.uploadKbps)));
			meter->setValue(std::clamp(static_cast<int>(std::lround(budget.usagePercent())), 0, 100));
			setStyleProperty(meter, "rdTone",
					 budget.status == BandwidthStatus::Exceeded ? QStringLiteral("error")
					 : budget.status == BandwidthStatus::Tight  ? QStringLiteral("warning")
										     : QString());
			meter->show();
			const UserMessage message = bandwidthMessage(budget);
			detail->setText(budget.status == BandwidthStatus::Ok
						? uiTextF("Dock.Network.Safe", "Safe limit {0}.", formatBitrate(budget.safeLimitKbps))
						: qs(message.detail) + QStringLiteral(" ") + qs(message.action));
		}

		QStringList lines;
		for (const BandwidthEntry &entry : budget.entries) {
			lines << (entry.measuredKbps > 0 ? uiTextF("Network.Entry.Live", "{0}: needs {1}, sends {2} now", entry.name,
								   formatBitrate(entry.requiredKbps), formatBitrate(entry.measuredKbps))
							 : uiTextF("Network.Entry", "{0}: needs {1}", entry.name, formatBitrate(entry.requiredKbps)));
		}
		perDestination->setText(lines.join(QStringLiteral("\n")));
	});
}

// ---- Appearance ------------------------------------------------------------------------------------

void SettingsDialog::buildAppearance()
{
	PageBuilder p = beginPage(uiText("Settings.Appearance", "Appearance"),
				  uiText("Appearance.Intro", "These settings change RelayDock's own windows. OBS keeps its theme."));
	AppContext &app = host_.app();
	auto *form = new QFormLayout();

	auto *mode = new QComboBox(p.page);
	mode->addItem(uiText("Appearance.Mode.Obs", "Follow OBS"), QStringLiteral("follow_obs"));
	mode->addItem(uiText("Appearance.Mode.Light", "Light"), QStringLiteral("light"));
	mode->addItem(uiText("Appearance.Mode.Dark", "Dark"), QStringLiteral("dark"));
	mode->addItem(uiText("Appearance.Mode.Custom", "Custom"), QStringLiteral("custom"));
	form->addRow(uiText("Appearance.Mode", "Theme"), mode);

	auto *accentRow = new QHBoxLayout();
	auto *accent = new QPushButton(p.page);
	auto *accentReset = new QPushButton(uiText("Appearance.UseDefault", "Use default"), p.page);
	accentRow->addWidget(accent);
	accentRow->addWidget(accentReset);
	accentRow->addStretch(1);
	form->addRow(uiText("Appearance.Accent", "Accent colour"), accentRow);

	auto *background = new QPushButton(p.page);
	background->setToolTip(uiText("Appearance.Background.Tip", "Available with the Custom theme. RelayDock picks light or dark text to match."));
	form->addRow(uiText("Appearance.Background", "Background colour"), background);

	auto *imageRow = new QHBoxLayout();
	auto *image = new QLineEdit(p.page);
	image->setReadOnly(true);
	image->setPlaceholderText(uiText("Appearance.Image.None", "No image"));
	auto *browse = new QPushButton(uiText("Appearance.Image.Browse", "Choose..."), p.page);
	auto *clearImage = new QPushButton(uiText("Appearance.Image.Clear", "Remove"), p.page);
	imageRow->addWidget(image, 1);
	imageRow->addWidget(browse);
	imageRow->addWidget(clearImage);
	form->addRow(uiText("Appearance.Image", "Background image"), imageRow);

	auto *fit = new QComboBox(p.page);
	fit->addItem(uiText("Appearance.Fit.Fill", "Fill"), QStringLiteral("fill"));
	fit->addItem(uiText("Appearance.Fit.Fit", "Fit"), QStringLiteral("fit"));
	fit->addItem(uiText("Appearance.Fit.Stretch", "Stretch"), QStringLiteral("stretch"));
	fit->addItem(uiText("Appearance.Fit.Tile", "Tile"), QStringLiteral("tile"));
	fit->addItem(uiText("Appearance.Fit.Center", "Centre"), QStringLiteral("center"));
	form->addRow(uiText("Appearance.Fit", "Image placement"), fit);

	auto makeSlider = [&](int minimum, int maximum, const QString &name) {
		auto *slider = new QSlider(Qt::Horizontal, p.page);
		slider->setRange(minimum, maximum);
		slider->setAccessibleName(name);
		return slider;
	};
	auto *imageOpacity = makeSlider(0, 100, uiText("Appearance.ImageOpacity", "Image opacity"));
	form->addRow(uiText("Appearance.ImageOpacity", "Image opacity"), imageOpacity);
	auto *cardOpacity = makeSlider(20, 100, uiText("Appearance.CardOpacity", "Card opacity"));
	form->addRow(uiText("Appearance.CardOpacity", "Card opacity"), cardOpacity);

	auto *radius = new QSpinBox(p.page);
	radius->setRange(0, 16);
	radius->setSuffix(QStringLiteral(" px"));
	form->addRow(uiText("Appearance.Radius", "Corner radius"), radius);

	auto *fontScale = new QSpinBox(p.page);
	fontScale->setRange(75, 150);
	fontScale->setSingleStep(5);
	fontScale->setSuffix(QStringLiteral("%"));
	form->addRow(uiText("Appearance.FontScale", "Text size"), fontScale);

	auto *density = new QComboBox(p.page);
	density->addItem(uiText("Appearance.Density.Comfortable", "Comfortable"), QStringLiteral("comfortable"));
	density->addItem(uiText("Appearance.Density.Compact", "Compact"), QStringLiteral("compact"));
	form->addRow(uiText("Appearance.Density", "Spacing"), density);
	p.body->addLayout(form);

	auto *animations = new QCheckBox(uiText("Appearance.Animations", "Animate status changes"), p.page);
	auto *reducedMotion = new QCheckBox(uiText("Appearance.ReducedMotion", "Reduce motion"), p.page);
	reducedMotion->setToolTip(uiText("Appearance.ReducedMotion.Tip", "Switches every animation off, whatever the setting above says."));
	p.body->addWidget(animations);
	p.body->addWidget(reducedMotion);
	addNote(p.body, uiText("Appearance.Potato.Note", "Potato Mode switches animations off as well."));

	auto *logos = new QCheckBox(uiText("Appearance.Logos", "Show platform logos"), p.page);
	logos->setToolTip(uiText("Appearance.Logos.Tip", "Off shows each platform's initials instead of its logo."));
	p.body->addWidget(logos);

	auto *reset = new QPushButton(uiText("Appearance.Reset", "Reset appearance"), p.page);
	p.body->addWidget(reset, 0, Qt::AlignLeft);
	p.body->addStretch(1);

	auto pickColor = [this](const std::string &current, const QString &title) -> QString {
		Rgb start{0x80, 0x80, 0x80};
		parseHexColor(current, start);
		const QColor chosen = QColorDialog::getColor(toQColor(start), this, title);
		return chosen.isValid() ? chosen.name() : QString();
	};

	connect(mode, &QComboBox::activated, this, [=, this, &app](int index) {
		themeModeFromName(ss(mode->itemData(index).toString()), app.config().theme.mode);
		commit();
	});
	connect(accent, &QPushButton::clicked, this, [=, this, &app] {
		const QString chosen = pickColor(app.config().theme.accentColor, uiText("Appearance.Accent", "Accent colour"));
		if (chosen.isEmpty())
			return;
		app.config().theme.accentColor = ss(chosen);
		commit();
	});
	connect(accentReset, &QPushButton::clicked, this, [this, &app] {
		app.config().theme.accentColor.clear();
		commit();
	});
	connect(background, &QPushButton::clicked, this, [=, this, &app] {
		const QString chosen = pickColor(app.config().theme.backgroundColor, uiText("Appearance.Background", "Background colour"));
		if (chosen.isEmpty())
			return;
		app.config().theme.backgroundColor = ss(chosen);
		commit();
	});
	connect(browse, &QPushButton::clicked, this, [this, &app] {
		const QString file = QFileDialog::getOpenFileName(this, uiText("Appearance.Image.Title", "Choose a background image"),
								  QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
								  uiText("Appearance.Image.Filter", "Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
		if (file.isEmpty())
			return;
		app.config().theme.backgroundImage = ss(file);
		commit();
	});
	connect(clearImage, &QPushButton::clicked, this, [this, &app] {
		app.config().theme.backgroundImage.clear();
		commit();
	});
	connect(fit, &QComboBox::activated, this, [=, this, &app](int index) {
		backgroundFitFromName(ss(fit->itemData(index).toString()), app.config().theme.backgroundFit);
		commit();
	});
	connect(imageOpacity, &QSlider::valueChanged, this, [this, &app](int value) {
		app.config().theme.backgroundOpacity = value;
		commit();
	});
	connect(cardOpacity, &QSlider::valueChanged, this, [this, &app](int value) {
		app.config().theme.cardOpacity = value;
		commit();
	});
	connect(radius, &QSpinBox::valueChanged, this, [this, &app](int value) {
		app.config().theme.cornerRadius = value;
		commit();
	});
	connect(fontScale, &QSpinBox::valueChanged, this, [this, &app](int value) {
		app.config().theme.fontScale = value;
		commit();
	});
	connect(density, &QComboBox::activated, this, [=, this, &app](int index) {
		densityFromName(ss(density->itemData(index).toString()), app.config().theme.density);
		commit();
	});
	connect(animations, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().theme.animations = on;
		commit();
	});
	connect(reducedMotion, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().theme.reducedMotion = on;
		commit();
	});
	connect(logos, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().theme.platformLogos = on;
		commit();
	});
	connect(reset, &QPushButton::clicked, this, [this, &app] {
		app.config().theme = ThemeConfig{};
		commit();
	});

	addPage("appearance", uiText("Settings.Appearance", "Appearance"), "palette", p.page, [=, &app] {
		const ThemeConfig &theme = app.config().theme;
		mode->setCurrentIndex(mode->findData(QString::fromUtf8(themeModeName(theme.mode))));
		paintSwatch(accent, qs(theme.accentColor), uiText("Appearance.Accent.Default", "Theme default"));
		accentReset->setEnabled(!theme.accentColor.empty());
		const bool custom = theme.mode == ThemeMode::Custom;
		paintSwatch(background, custom ? qs(theme.backgroundColor.empty() ? std::string("#1e1f22") : theme.backgroundColor) : QString(),
			    uiText("Appearance.Background.Custom", "Custom theme only"));
		background->setEnabled(custom);
		image->setText(qs(theme.backgroundImage));
		const bool hasImage = !theme.backgroundImage.empty();
		clearImage->setEnabled(hasImage);
		fit->setEnabled(hasImage);
		imageOpacity->setEnabled(hasImage);
		fit->setCurrentIndex(fit->findData(QString::fromUtf8(backgroundFitName(theme.backgroundFit))));
		if (!imageOpacity->isSliderDown())
			imageOpacity->setValue(theme.backgroundOpacity);
		if (!cardOpacity->isSliderDown())
			cardOpacity->setValue(theme.cardOpacity);
		if (!radius->hasFocus())
			radius->setValue(theme.cornerRadius);
		if (!fontScale->hasFocus())
			fontScale->setValue(theme.fontScale);
		density->setCurrentIndex(density->findData(QString::fromUtf8(densityName(theme.density))));
		animations->setChecked(theme.animations);
		reducedMotion->setChecked(theme.reducedMotion);
		animations->setEnabled(!theme.reducedMotion);
		logos->setChecked(theme.platformLogos);
	});
}

// ---- Layout ----------------------------------------------------------------------------------------

void SettingsDialog::buildLayout()
{
	PageBuilder p = beginPage(uiText("Settings.Layout", "Layout"),
				  uiText("Layout.Intro",
					 "A layout remembers how the dock is arranged. Keep one for streaming and one for setting up, and switch between them here."));
	AppContext &app = host_.app();

	addHeading(p.body, uiText("Layout.Saved", "Saved layouts"));
	auto *list = new QListWidget(p.page);
	list->setAccessibleName(uiText("Layout.Saved", "Saved layouts"));
	list->setMaximumHeight(130);
	p.body->addWidget(list);

	auto *buttons = new QHBoxLayout();
	auto *load = new QPushButton(uiText("Layout.Load", "Load"), p.page);
	load->setObjectName(QStringLiteral("rdPrimary"));
	auto *saveAs = new QPushButton(uiText("Layout.Save", "Save as new..."), p.page);
	auto *duplicate = new QPushButton(uiText("Layout.Duplicate", "Duplicate"), p.page);
	auto *rename = new QPushButton(uiText("Layout.Rename", "Rename..."), p.page);
	auto *remove = new QPushButton(uiText("Layout.Delete", "Delete..."), p.page);
	auto *reset = new QPushButton(uiText("Layout.Reset", "Reset"), p.page);
	reset->setToolTip(uiText("Layout.Reset.Tip", "Puts the layout in use back to RelayDock's default arrangement."));
	for (QPushButton *button : {load, saveAs, duplicate, rename, remove, reset})
		buttons->addWidget(button);
	buttons->addStretch(1);
	p.body->addLayout(buttons);

	addHeading(p.body, uiText("Layout.Current", "The layout in use"));
	auto *form = new QFormLayout();
	auto *placement = new QComboBox(p.page);
	placement->addItem(uiText("Layout.Placement.Top", "Above the cards"), QStringLiteral("top"));
	placement->addItem(uiText("Layout.Placement.Bottom", "Below the cards"), QStringLiteral("bottom"));
	form->addRow(uiText("Layout.Placement", "Buttons"), placement);
	auto *cardView = new QComboBox(p.page);
	cardView->addItem(uiText("Layout.Cards.Expanded", "Expanded, with details"), QStringLiteral("expanded"));
	cardView->addItem(uiText("Layout.Cards.Compact", "Compact"), QStringLiteral("compact"));
	form->addRow(uiText("Layout.Cards", "Cards"), cardView);
	auto *listMode = new QComboBox(p.page);
	listMode->addItem(uiText("Layout.Mode.List", "List"), QStringLiteral("list"));
	listMode->addItem(uiText("Layout.Mode.Grid", "Grid, side by side when the dock is wide"), QStringLiteral("grid"));
	form->addRow(uiText("Layout.Mode", "Arrangement"), listMode);
	p.body->addLayout(form);

	auto *showSuggestions = new QCheckBox(uiText("Layout.Show.Suggestions", "Show suggestions"), p.page);
	auto *showPerformance = new QCheckBox(uiText("Layout.Show.Performance", "Show the performance panel"), p.page);
	auto *showNetwork = new QCheckBox(uiText("Layout.Show.Network", "Show the network panel"), p.page);
	p.body->addWidget(showSuggestions);
	p.body->addWidget(showPerformance);
	p.body->addWidget(showNetwork);

	addHeading(p.body, uiText("Layout.Order", "Order of the sections"));
	auto *orderRow = new QHBoxLayout();
	auto *order = new QListWidget(p.page);
	order->setAccessibleName(uiText("Layout.Order", "Order of the sections"));
	order->setFixedHeight(142);
	auto *orderButtons = new QVBoxLayout();
	auto *up = new QPushButton(uiText("Layout.Order.Up", "Move up"), p.page);
	auto *down = new QPushButton(uiText("Layout.Order.Down", "Move down"), p.page);
	orderButtons->addWidget(up);
	orderButtons->addWidget(down);
	orderButtons->addStretch(1);
	orderRow->addWidget(order, 1);
	orderRow->addLayout(orderButtons);
	p.body->addLayout(orderRow);
	p.body->addStretch(1);

	auto selectedId = [list] { return list->currentItem() ? ss(list->currentItem()->data(Qt::UserRole).toString()) : std::string(); };
	auto askName = [this](const QString &title, const QString &initial) {
		bool ok = false;
		const QString name =
			QInputDialog::getText(this, title, uiText("Layout.Name", "Name"), QLineEdit::Normal, initial, &ok).trimmed();
		return ok ? name.left(64) : QString();
	};

	connect(load, &QPushButton::clicked, this, [=, this, &app] {
		const std::string id = selectedId();
		if (id.empty() || !app.config().findLayout(id))
			return;
		app.config().activeLayoutId = id;
		commit();
	});
	connect(list, &QListWidget::itemDoubleClicked, this, [=, this, &app] {
		const std::string id = selectedId();
		if (id.empty() || !app.config().findLayout(id))
			return;
		app.config().activeLayoutId = id;
		commit();
	});
	connect(saveAs, &QPushButton::clicked, this, [=, this, &app] {
		const QString name = askName(uiText("Layout.Save.Title", "Save layout"), uiText("Layout.Save.Default", "My layout"));
		if (name.isEmpty())
			return;
		DockLayout copy = app.config().activeLayout();
		copy.id = generateUuid();
		copy.name = ss(name);
		app.config().layouts.push_back(copy);
		app.config().activeLayoutId = copy.id;
		commit();
	});
	connect(duplicate, &QPushButton::clicked, this, [=, this, &app] {
		const DockLayout *source = app.config().findLayout(selectedId());
		if (!source)
			return;
		DockLayout copy = *source;
		copy.id = generateUuid();
		copy.name = locf("Destination.CopyName", "{0} copy", source->name);
		app.config().layouts.push_back(copy);
		commit();
	});
	connect(rename, &QPushButton::clicked, this, [=, this, &app] {
		DockLayout *layout = app.config().findLayout(selectedId());
		if (!layout)
			return;
		const QString name = askName(uiText("Layout.Rename.Title", "Rename layout"), qs(layout->name));
		if (name.isEmpty())
			return;
		// Look it up again. The input box ran an event loop.
		if (DockLayout *current = app.config().findLayout(selectedId()))
			current->name = ss(name);
		commit();
	});
	connect(remove, &QPushButton::clicked, this, [=, this, &app] {
		const std::string id = selectedId();
		auto &layouts = app.config().layouts;
		const auto found = std::find_if(layouts.begin(), layouts.end(), [&](const DockLayout &l) { return l.id == id; });
		if (found == layouts.end() || layouts.size() <= 1)
			return;
		QMessageBox box(QMessageBox::Question, uiText("Layout.Delete.Title", "Delete layout"),
				uiTextF("Layout.Delete.Question", "Delete the layout {0}?", found->name), QMessageBox::Yes | QMessageBox::Cancel, this);
		box.setDefaultButton(QMessageBox::Cancel);
		if (box.exec() != QMessageBox::Yes)
			return;
		const auto again = std::find_if(layouts.begin(), layouts.end(), [&](const DockLayout &l) { return l.id == id; });
		if (again == layouts.end() || layouts.size() <= 1)
			return;
		layouts.erase(again);
		if (app.config().activeLayoutId == id)
			app.config().activeLayoutId = layouts.front().id;
		commit();
	});
	connect(reset, &QPushButton::clicked, this, [this, &app] {
		DockLayout &active = app.config().activeLayout();
		DockLayout fresh;
		fresh.id = active.id;
		fresh.name = active.name;
		active = fresh;
		commit();
	});

	connect(placement, &QComboBox::activated, this, [=, this, &app](int index) {
		controlPlacementFromName(ss(placement->itemData(index).toString()), app.config().activeLayout().controlPlacement);
		commit();
	});
	connect(cardView, &QComboBox::activated, this, [=, this, &app](int index) {
		cardViewFromName(ss(cardView->itemData(index).toString()), app.config().activeLayout().cardView);
		commit();
	});
	connect(listMode, &QComboBox::activated, this, [=, this, &app](int index) {
		listModeFromName(ss(listMode->itemData(index).toString()), app.config().activeLayout().listMode);
		commit();
	});
	connect(showSuggestions, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().activeLayout().showSuggestions = on;
		commit();
	});
	connect(showPerformance, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().activeLayout().showPerformance = on;
		commit();
	});
	connect(showNetwork, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().activeLayout().showNetwork = on;
		commit();
	});

	auto moveSection = [=, this, &app](int delta) {
		const int row = order->currentRow();
		auto &sections = app.config().activeLayout().sectionOrder;
		const int target = row + delta;
		if (row < 0 || target < 0 || target >= static_cast<int>(sections.size()))
			return;
		std::swap(sections[static_cast<size_t>(row)], sections[static_cast<size_t>(target)]);
		order->setProperty("rdWanted", target);
		commit();
	};
	connect(up, &QPushButton::clicked, this, [moveSection] { moveSection(-1); });
	connect(down, &QPushButton::clicked, this, [moveSection] { moveSection(1); });

	addPage("layout", uiText("Settings.Layout", "Layout"), "layout-grid", p.page, [=, &app] {
		const QString previous = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
		const DockLayout &active = app.config().activeLayout();
		list->clear();
		for (const DockLayout &layout : app.config().layouts) {
			const bool inUse = layout.id == active.id;
			auto *item = new QListWidgetItem(inUse ? uiTextF("Layout.Item.Active", "{0} (in use)", layout.name) : qs(layout.name), list);
			item->setData(Qt::UserRole, qs(layout.id));
			if (qs(layout.id) == previous || (previous.isEmpty() && inUse))
				list->setCurrentItem(item);
		}
		remove->setEnabled(app.config().layouts.size() > 1);

		placement->setCurrentIndex(placement->findData(QString::fromUtf8(controlPlacementName(active.controlPlacement))));
		cardView->setCurrentIndex(cardView->findData(QString::fromUtf8(cardViewName(active.cardView))));
		listMode->setCurrentIndex(listMode->findData(QString::fromUtf8(listModeName(active.listMode))));
		showSuggestions->setChecked(active.showSuggestions);
		showPerformance->setChecked(active.showPerformance);
		showNetwork->setChecked(active.showNetwork);

		const QVariant wanted = order->property("rdWanted");
		const int row = wanted.isValid() ? wanted.toInt() : std::max(0, order->currentRow());
		order->setProperty("rdWanted", QVariant());
		order->clear();
		for (const std::string &id : active.sectionOrder)
			order->addItem(sectionTitle(id));
		order->setCurrentRow(std::min(row, order->count() - 1));
	});
}

// ---- Security --------------------------------------------------------------------------------------

void SettingsDialog::buildSecurity()
{
	PageBuilder p = beginPage(uiText("Settings.Security", "Security"),
				  uiText("Security.Intro",
					 "Stream keys and passwords are saved in Windows Credential Manager, encrypted for your Windows account. They are never written to RelayDock's settings file, to OBS settings or to a log."));
	AppContext &app = host_.app();

	QLabel *backend = addNote(p.body, QString());
	addHeading(p.body, uiText("Security.Saved", "Saved keys and passwords"));
	auto *list = new QListWidget(p.page);
	list->setAccessibleName(uiText("Security.Saved", "Saved keys and passwords"));
	list->setMinimumHeight(140);
	p.body->addWidget(list);
	addNote(p.body, uiText("Security.Saved.Note", "The list shows which secrets exist, never their values."));

	auto *buttons = new QHBoxLayout();
	auto *removeOne = new QPushButton(uiText("Security.Remove", "Remove selected..."), p.page);
	auto *removeOrphans = new QPushButton(uiText("Security.Orphans", "Remove keys of deleted destinations"), p.page);
	auto *removeAll = new QPushButton(uiText("Security.RemoveAll", "Remove all..."), p.page);
	buttons->addWidget(removeOne);
	buttons->addWidget(removeOrphans);
	buttons->addWidget(removeAll);
	buttons->addStretch(1);
	p.body->addLayout(buttons);

	addHeading(p.body, uiText("Security.Logs", "Logs and diagnostics"));
	addNote(p.body, uiText("Security.Logs.Note",
			       "RelayDock removes every key and password it has handled from its log lines and from the diagnostics export. It also removes text that looks like a credential in a server address. OBS writes each server address to its own log, so put secrets in the Stream key field and never in the Server URL."));
	auto *notice = new QPushButton(uiText("Security.Notice", "Read the Security and Credentials Notice"), p.page);
	p.body->addWidget(notice, 0, Qt::AlignLeft);
	p.body->addStretch(1);

	connect(notice, &QPushButton::clicked, this, [this] { host_.showLegal("security-notice"); });
	connect(removeOne, &QPushButton::clicked, this, [=, this, &app] {
		QListWidgetItem *item = list->currentItem();
		if (!item)
			return;
		CredentialKind kind = CredentialKind::StreamKey;
		credentialKindFromName(ss(item->data(Qt::UserRole + 1).toString()), kind);
		const CredentialId id{ss(item->data(Qt::UserRole).toString()), kind};
		QMessageBox box(QMessageBox::Question, uiText("Security.Remove.Title", "Remove saved secret"),
				uiTextF("Security.Remove.Question", "Remove {0}?", ss(item->text())), QMessageBox::Yes | QMessageBox::Cancel, this);
		box.setInformativeText(uiText("Security.Remove.Detail",
					      "RelayDock deletes it from Windows Credential Manager. The key stays valid on the platform. A stream that is live keeps running."));
		box.setDefaultButton(QMessageBox::Cancel);
		if (box.exec() != QMessageBox::Yes)
			return;
		app.vault().remove(id);
		commit();
		refreshCurrent();
	});
	connect(removeOrphans, &QPushButton::clicked, this, [this, &app] {
		std::vector<std::string> known;
		for (const DestinationConfig &destination : app.config().destinations)
			known.push_back(destination.id);
		const size_t removed = app.vault().removeOrphans(known);
		logInfo("Removed {} saved secret(s) of deleted destinations.", removed);
		refreshCurrent();
	});
	connect(removeAll, &QPushButton::clicked, this, [this, &app] {
		QMessageBox box(QMessageBox::Warning, uiText("Security.RemoveAll.Title", "Remove all saved secrets"),
				uiText("Security.RemoveAll.Question", "Remove every stream key and password RelayDock saved?"),
				QMessageBox::Yes | QMessageBox::Cancel, this);
		box.setInformativeText(uiText("Security.RemoveAll.Detail",
					      "You have to enter each key again before the destination can start. Keys stay valid on the platforms."));
		box.setDefaultButton(QMessageBox::Cancel);
		if (box.exec() != QMessageBox::Yes)
			return;
		const size_t removed = app.vault().removeAll();
		logInfo("Removed all {} saved secret(s) on request.", removed);
		commit();
		refreshCurrent();
	});

	addPage("security", uiText("Settings.Security", "Security"), "shield-check", p.page, [=, &app] {
		backend->setText(uiTextF("Security.Backend", "Storage in use: {0}.", app.vault().backendName()));
		list->clear();
		bool anyOrphan = false;
		for (const CredentialId &id : app.vault().list()) {
			const DestinationConfig *config = app.config().findDestination(id.destinationId);
			anyOrphan = anyOrphan || !config;
			const std::string owner = config ? config->name : loc("Security.Orphan", "A deleted destination");
			QString text = id.kind == CredentialKind::StreamKey ? uiTextF("Security.Item.Key", "{0}: stream key", owner)
									    : uiTextF("Security.Item.Password", "{0}: password", owner);
			if (app.vault().sessionOnly(id))
				text += QStringLiteral(" ") + uiText("Security.Item.SessionOnly", "(kept in memory only, until OBS closes)");
			auto *item = new QListWidgetItem(text, list);
			item->setData(Qt::UserRole, qs(id.destinationId));
			item->setData(Qt::UserRole + 1, QString::fromUtf8(credentialKindName(id.kind)));
		}
		removeOne->setEnabled(list->count() > 0);
		removeAll->setEnabled(list->count() > 0);
		removeOrphans->setEnabled(anyOrphan);
		if (list->count() > 0)
			list->setCurrentRow(0);
	});
}

// ---- Diagnostics -----------------------------------------------------------------------------------

void SettingsDialog::buildDiagnostics()
{
	PageBuilder p = beginPage(uiText("Settings.Diagnostics", "Diagnostics"),
				  uiText("Diagnostics.Intro",
					 "Tools for finding out why something does not work. Nothing here leaves your PC unless you send it to someone yourself."));
	AppContext &app = host_.app();

	addHeading(p.body, uiText("Diagnostics.Preflight", "Preflight check"));
	auto *preflight = new QPushButton(uiText("Diagnostics.Preflight.Run", "Run the check"), p.page);
	p.body->addWidget(preflight, 0, Qt::AlignLeft);

	addHeading(p.body, uiText("Diagnostics.Export", "Diagnostics report"));
	addNote(p.body, uiText("Diagnostics.Export.Note",
			       "A text report of your settings, your PC, the state of each destination and RelayDock's recent log lines. It contains no stream key and no password, server addresses are shortened, and your Windows user name is removed. Read it before you share it."));
	auto *exportRow = new QHBoxLayout();
	auto *exportButton = new QPushButton(uiText("Diagnostics.Export.Save", "Save report..."), p.page);
	auto *copyButton = new QPushButton(uiText("Diagnostics.Export.Copy", "Copy report"), p.page);
	exportRow->addWidget(exportButton);
	exportRow->addWidget(copyButton);
	exportRow->addStretch(1);
	p.body->addLayout(exportRow);
	QLabel *exportResult = addNote(p.body, QString());

	addHeading(p.body, uiText("Diagnostics.Log", "Recent RelayDock log lines"));
	auto *log = new QPlainTextEdit(p.page);
	log->setReadOnly(true);
	log->setLineWrapMode(QPlainTextEdit::NoWrap);
	log->setMinimumHeight(200);
	log->setAccessibleName(uiText("Diagnostics.Log", "Recent RelayDock log lines"));
	p.body->addWidget(log, 1);
	auto *refreshLog = new QPushButton(uiText("Diagnostics.Log.Refresh", "Refresh"), p.page);
	p.body->addWidget(refreshLog, 0, Qt::AlignLeft);

	connect(preflight, &QPushButton::clicked, this, [this] { host_.runPreflight(false); });
	connect(exportButton, &QPushButton::clicked, this, [=, this, &app] {
		const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/RelayDock-diagnostics-") +
					  QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")) + QStringLiteral(".txt");
		const QString file = QFileDialog::getSaveFileName(this, uiText("Diagnostics.Export.Title", "Save diagnostics report"), suggested,
								  uiText("Diagnostics.Export.Filter", "Text files (*.txt)"));
		if (file.isEmpty())
			return;
		const std::string report = buildDiagnosticsNow(app);
		QFile out(file);
		if (out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(report.data(), static_cast<qint64>(report.size())) >= 0) {
			exportResult->setText(uiTextF("Diagnostics.Export.Done", "Saved to {0}.", ss(file)));
			setStyleProperty(exportResult, "rdTone", QStringLiteral("ok"));
		} else {
			exportResult->setText(uiTextF("Diagnostics.Export.Failed", "RelayDock could not write {0}. {1}", ss(file), ss(out.errorString())));
			setStyleProperty(exportResult, "rdTone", QStringLiteral("error"));
		}
	});
	connect(copyButton, &QPushButton::clicked, this, [=, &app] {
		QApplication::clipboard()->setText(qs(buildDiagnosticsNow(app)));
		exportResult->setText(uiText("Diagnostics.Export.Copied", "The report is on the clipboard."));
		setStyleProperty(exportResult, "rdTone", QStringLiteral("ok"));
	});
	auto loadLog = [log] {
		QStringList lines;
		for (const std::string &line : recentLogLines(kRecentLogCapacity))
			lines << qs(line);
		log->setPlainText(lines.join(QStringLiteral("\n")));
		log->moveCursor(QTextCursor::End);
	};
	connect(refreshLog, &QPushButton::clicked, this, loadLog);

	addPage("diagnostics", uiText("Settings.Diagnostics", "Diagnostics"), "stethoscope", p.page, loadLog);
}

// ---- Updates ---------------------------------------------------------------------------------------

void SettingsDialog::buildUpdates()
{
	PageBuilder p = beginPage(uiText("Settings.Updates", "Updates"), QString());
	AppContext &app = host_.app();
	const std::string repository = repositoryUrl();
	const std::string version = buildInfo().version;

	p.body->addWidget(makeLabel(uiTextF("Updates.Current", "You run RelayDock {0}.", buildVersionString()), "rdTitle", p.page));

	if (repository.empty()) {
		// A build without a project page has nothing to ask.
		addNote(p.body, uiText("Updates.NotConfigured",
				       "This build has no project page configured, so it has no update check. Get new versions from the place you got this one."));
		p.body->addStretch(1);
		addPage("updates", uiText("Settings.Updates", "Updates"), "download", p.page, nullptr);
		return;
	}

	addNote(p.body, uiText("Updates.Note",
			       "The check asks GitHub for the newest release and compares version numbers. It sends nothing about you or your PC, and it never downloads or installs anything."));
	auto *check = new QPushButton(uiText("Updates.Check", "Check for updates"), p.page);
	p.body->addWidget(check, 0, Qt::AlignLeft);
	auto *result = new Banner(host_.theme(), p.page);
	result->hide();
	p.body->addWidget(result);
	auto *onStart = new QCheckBox(uiText("Updates.OnStart", "Check when OBS starts"), p.page);
	p.body->addWidget(onStart);
	addNote(p.body, uiText("Updates.OnStart.Note",
			       "On unless you switch it off. With it on, RelayDock asks GitHub once each time OBS starts and tells you when a newer version exists."));
	p.body->addStretch(1);

	updateChecker_ = new UpdateChecker(this);
	auto offered = std::make_shared<ReleaseInfo>();
	connect(updateChecker_, &UpdateChecker::finished, this, [=, this](const UpdateResult &update) {
		check->setEnabled(true);
		const UserMessage message = describeUpdate(update, version);
		const bool available = update.status == UpdateStatus::UpdateAvailable;
		const char *tone = available                                    ? "warning"
				   : update.status == UpdateStatus::UpToDate ? "ok"
				   : update.status == UpdateStatus::Failed   ? "error"
									     : "neutral";
		result->setMessage(tone, message);
		*offered = available ? update.release : ReleaseInfo{};
		result->setAction(available ? uiText("Update.Show", "How to update") : QString());
		result->show();
	});
	// Whoever asks by hand is told, also about a version they skipped before.
	connect(result, &Banner::actionClicked, this, [this, offered] { host_.showUpdate(*offered); });
	connect(check, &QPushButton::clicked, this, [=, this] {
		check->setEnabled(false);
		result->setMessage("neutral", uiText("Updates.Checking", "Asking GitHub for the newest release..."));
		result->setAction(QString());
		result->show();
		updateChecker_->start(repository, version);
	});
	connect(onStart, &QCheckBox::toggled, this, [this, &app](bool on) {
		app.config().general.checkUpdatesOnStart = on;
		commit();
	});

	addPage("updates", uiText("Settings.Updates", "Updates"), "download", p.page,
		[=, &app] { onStart->setChecked(app.config().general.checkUpdatesOnStart); });
}

// ---- Advanced --------------------------------------------------------------------------------------

void SettingsDialog::buildAdvanced()
{
	PageBuilder p = beginPage(uiText("Settings.Advanced", "Advanced"), QString());
	AppContext &app = host_.app();

	addHeading(p.body, uiText("Advanced.Folder", "Settings folder"));
	const QString folder = qs(pathToUtf8(app.configStore().directory()));
	QLabel *path = addNote(p.body, folder);
	path->setTextInteractionFlags(Qt::TextSelectableByMouse);
	addNote(p.body, uiText("Advanced.Folder.Note",
			       "config.json holds your destinations and settings. config.json.bak is the copy from before the last save. Neither contains a stream key."));
	auto *open = new QPushButton(uiText("Advanced.Folder.Open", "Open folder"), p.page);
	p.body->addWidget(open, 0, Qt::AlignLeft);

	addHeading(p.body, uiText("Advanced.Notes", "Notes from loading the settings"));
	QLabel *notes = addNote(p.body, QString());

	addHeading(p.body, uiText("Advanced.Reset", "Reset"));
	addNote(p.body, uiText("Advanced.Reset.Note",
			       "Removes every destination and puts every setting back to its default. Your record of the legal documents you accepted stays. Vertical layouts stay too, because they belong to the OBS scene collection."));
	auto *reset = new QPushButton(uiText("Advanced.Reset.Button", "Reset RelayDock..."), p.page);
	p.body->addWidget(reset, 0, Qt::AlignLeft);
	QLabel *resetNote = addNote(p.body, QString());
	p.body->addStretch(1);

	connect(open, &QPushButton::clicked, this, [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
	connect(reset, &QPushButton::clicked, this, [=, this, &app] {
		if (app.outputs().anyActive()) {
			resetNote->setText(uiText("Advanced.Reset.Active", "Stop every destination before you reset RelayDock."));
			setStyleProperty(resetNote, "rdTone", QStringLiteral("warning"));
			return;
		}
		QMessageBox box(QMessageBox::Warning, uiText("Advanced.Reset.Title", "Reset RelayDock"),
				uiText("Advanced.Reset.Question", "Remove every destination and reset every setting?"),
				QMessageBox::Yes | QMessageBox::Cancel, this);
		auto *alsoKeys = new QCheckBox(uiText("Advanced.Reset.Keys", "Also delete the saved stream keys and passwords"), &box);
		alsoKeys->setChecked(true);
		box.setCheckBox(alsoKeys);
		box.setInformativeText(uiText("Advanced.Reset.Detail", "This cannot be undone."));
		box.setDefaultButton(QMessageBox::Cancel);
		if (box.exec() != QMessageBox::Yes || app.outputs().anyActive())
			return;

		for (const DestinationConfig &destination : app.config().destinations)
			app.outputs().forget(destination.id);
		if (alsoKeys->isChecked())
			app.vault().removeAll();
		AppConfig fresh = makeDefaultConfig();
		fresh.legal = app.config().legal;
		app.config() = fresh;
		app.clearAdjustments();
		logInfo("Settings were reset on request.");
		resetNote->setText(uiText("Advanced.Reset.Done", "RelayDock is back to its defaults."));
		setStyleProperty(resetNote, "rdTone", QStringLiteral("ok"));
		commit();
	});

	addPage("advanced", uiText("Settings.Advanced", "Advanced"), "wrench", p.page, [=, &app] {
		QStringList lines;
		for (const std::string &note : app.loadNotes())
			lines << qs(note);
		notes->setText(lines.isEmpty() ? uiText("Advanced.Notes.None", "The settings loaded without any repair.") : lines.join(QStringLiteral("\n")));
	});
}

// ---- About -----------------------------------------------------------------------------------------

void SettingsDialog::buildAbout()
{
	const BuildInfo &info = buildInfo();
	PageBuilder p = beginPage(qs(info.displayName),
				  uiText("About.Tagline",
					 "A free and open-source OBS Studio plugin that streams to several platforms from one OBS session, straight from your PC."));
	AppContext &app = host_.app();

	QStringList facts;
	facts << uiTextF("About.Version", "Version {0}", buildVersionString());
	facts << uiTextF("About.Build", "Build {0}, {1}, {2}", info.buildNumber, info.buildDate, info.architecture);
	if (*info.commit)
		facts << uiTextF("About.Commit", "Commit {0}{1}", info.commit, info.dirty ? loc("About.Dirty", ", with local changes") : std::string());
	facts << uiTextF("About.Obs", "Running in OBS Studio {0}. Needs {1} or newer. Tested with {2}.", obs_get_version_string(),
			 info.obsMinimumVersion, info.obsTestedVersions);
	if (isDevelopmentBuild())
		facts << uiText("About.Development", "This is a pre-release build.");
	QLabel *factLabel = addNote(p.body, facts.join(QStringLiteral("\n")));
	factLabel->setObjectName(QString());

	const std::string repository = repositoryUrl();
	if (!repository.empty()) {
		auto *project = new QPushButton(uiText("About.Project", "Open the project page"), p.page);
		project->setToolTip(qs(repository));
		const QString url = qs(repository);
		connect(project, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
		p.body->addWidget(project, 0, Qt::AlignLeft);
	}

	addHeading(p.body, uiText("About.Licence", "Licence"));
	addNote(p.body, uiTextF("About.Licence.Text",
				"Copyright (C) {0} {1}. RelayDock is free software under the GNU General Public License, version 2 or any later version. It comes with no warranty.",
				info.buildYear, info.author));
	addNote(p.body, uiText("About.Independent",
			       "RelayDock is not affiliated with Twitch, TikTok, YouTube, Facebook or the OBS Project. Their names belong to their owners."));

	addHeading(p.body, uiText("About.Legal", "Legal documents"));
	auto *documents = new QVBoxLayout();
	p.body->addLayout(documents);
	p.body->addStretch(1);

	addPage("about", uiText("Settings.About", "About"), "info", p.page, [=, this, &app] {
		while (QLayoutItem *item = documents->takeAt(0)) {
			if (QLayout *row = item->layout()) {
				while (QLayoutItem *child = row->takeAt(0)) {
					if (child->widget())
						child->widget()->deleteLater();
					delete child;
				}
			}
			delete item;
		}
		for (const LegalDocument &document : legalDocuments()) {
			auto *row = new QHBoxLayout();
			QString status = uiText("About.Legal.NotAccepted", "not reviewed yet");
			for (const LegalAcceptance &record : app.config().legal) {
				if (record.documentId != document.id)
					continue;
				status = record.version == document.version
						 ? uiTextF("About.Legal.Accepted", "version {0}, accepted {1} with RelayDock {2}", record.version,
							   record.acceptedAtUtc, record.appVersion)
						 : uiTextF("About.Legal.Outdated", "you accepted version {0}. Version {1} is waiting for review", record.version,
							   document.version);
			}
			QLabel *label = makeLabel(QStringLiteral("%1\n%2").arg(qs(document.title), status), nullptr, p.page);
			label->setWordWrap(true);
			auto *view = new QPushButton(uiText("About.Legal.View", "View"), p.page);
			view->setAccessibleName(uiTextF("About.Legal.View.Name", "View {0}", document.title));
			const std::string id = document.id;
			connect(view, &QPushButton::clicked, this, [this, id] { host_.showLegal(id); });
			row->addWidget(label, 1);
			row->addWidget(view, 0, Qt::AlignTop);
			documents->addLayout(row);
		}
	});
}

} // namespace rd
