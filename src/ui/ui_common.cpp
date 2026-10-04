// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/ui_common.h"

#include "app/app_context.h"

#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QFontInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QImageReader>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>

namespace rd {

QColor toQColor(Rgb color)
{
	return QColor(color.r, color.g, color.b);
}

Rgb fromQColor(const QColor &color)
{
	return {color.red(), color.green(), color.blue()};
}

// ---- Theme ---------------------------------------------------------------------------------------

Theme::Theme(AppContext &app, QObject *parent) : QObject(parent), app_(app)
{
	connect(&app_, &AppContext::configChanged, this, &Theme::refresh);
	refresh();
}

void Theme::refresh()
{
	// What the OBS theme uses right now.
	BasePalette base;
	const QPalette palette = QApplication::palette();
	base.window = fromQColor(palette.color(QPalette::Window));
	base.text = fromQColor(palette.color(QPalette::WindowText));
	base.highlight = fromQColor(palette.color(QPalette::Highlight));
	const int fontPx = QFontInfo(QApplication::font()).pixelSize();
	base.fontPx = fontPx > 0 ? fontPx : 13;

	const ThemeConfig &theme = app_.config().theme;
	colors_ = resolveThemeColors(theme, base);
	metrics_ = resolveThemeMetrics(theme, base);
	const QString styleSheet = qs(buildStyleSheet(colors_, metrics_));

	// The style sheet carries every colour and size, so an equal sheet means nothing changed,
	// unless the badges switched between logos and initials.
	if (styleSheet == styleSheet_ && theme.platformLogos == ProviderBadge::logosEnabled())
		return;
	ProviderBadge::setLogosEnabled(theme.platformLogos);
	styleSheet_ = styleSheet;
	pixmapCache_.clear();
	roots_.erase(std::remove_if(roots_.begin(), roots_.end(), [](const QPointer<QWidget> &root) { return root.isNull(); }),
		     roots_.end());
	for (const QPointer<QWidget> &root : roots_)
		root->setStyleSheet(styleSheet_);
	Q_EMIT changed();
}

void Theme::attach(QWidget *root)
{
	if (!root)
		return;
	root->setStyleSheet(styleSheet_);
	roots_.emplace_back(root);
}

bool Theme::animations() const
{
	return animationsAllowed(app_.config().theme, app_.config().performanceMode);
}

QColor Theme::color(Tone tone) const
{
	switch (tone) {
	case Tone::Text:
		return toQColor(colors_.text);
	case Tone::Muted:
		return toQColor(colors_.mutedText);
	case Tone::Accent:
		return toQColor(colors_.accent);
	case Tone::AccentText:
		return toQColor(colors_.accentText);
	case Tone::Ok:
		return toQColor(colors_.ok);
	case Tone::Warning:
		return toQColor(colors_.warning);
	case Tone::Error:
		return toQColor(colors_.error);
	}
	return toQColor(colors_.text);
}

QPixmap Theme::pixmap(const char *name, const QColor &color, int sizePx, qreal devicePixelRatio)
{
	const QString key = QStringLiteral("%1|%2|%3|%4").arg(QString::fromUtf8(name), color.name()).arg(sizePx).arg(devicePixelRatio);
	const auto cached = pixmapCache_.constFind(key);
	if (cached != pixmapCache_.constEnd())
		return *cached;

	QPixmap result;
	QFile file(QStringLiteral(":/relaydock/icons/%1.svg").arg(QString::fromUtf8(name)));
	if (file.open(QIODevice::ReadOnly)) {
		// Lucide icons draw with "currentColor". Put the wanted colour in its place.
		QByteArray svg = file.readAll();
		svg.replace("currentColor", color.name().toUtf8());

		QBuffer buffer(&svg);
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer, "svg");
		const int pixels = std::max(1, static_cast<int>(sizePx * devicePixelRatio + 0.5));
		reader.setScaledSize(QSize(pixels, pixels));
		const QImage image = reader.read();
		if (!image.isNull()) {
			result = QPixmap::fromImage(image);
			result.setDevicePixelRatio(devicePixelRatio);
		}
	}
	if (result.isNull()) {
		// Keep the layout stable even when an icon cannot be drawn.
		result = QPixmap(sizePx, sizePx);
		result.fill(Qt::transparent);
	}
	pixmapCache_.insert(key, result);
	return result;
}

QIcon Theme::icon(const char *name, Tone tone)
{
	const int px = metrics_.iconPx;
	const QColor normal = color(tone);
	const QColor disabled = color(Tone::Muted);
	QIcon icon;
	// Two scales cover standard and high-density screens.
	for (qreal ratio : {1.0, 2.0}) {
		icon.addPixmap(pixmap(name, normal, px, ratio), QIcon::Normal);
		icon.addPixmap(pixmap(name, disabled, px, ratio), QIcon::Disabled);
		// A selected list row has the accent colour behind it.
		icon.addPixmap(pixmap(name, color(Tone::AccentText), px, ratio), QIcon::Selected);
	}
	return icon;
}

// ---- Small widgets -------------------------------------------------------------------------------

void setStyleProperty(QWidget *widget, const char *name, const QString &value)
{
	if (widget->property(name).toString() == value)
		return;
	widget->setProperty(name, value);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

QLabel *makeLabel(const QString &text, const char *objectName, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	if (objectName)
		label->setObjectName(QString::fromUtf8(objectName));
	return label;
}

QToolButton *makeToolButton(Theme &theme, const char *iconName, const QString &tooltip, QWidget *parent)
{
	auto *button = new QToolButton(parent);
	button->setObjectName(QStringLiteral("rdTool"));
	button->setToolTip(tooltip);
	button->setAccessibleName(tooltip);
	button->setAutoRaise(true);
	button->setFocusPolicy(Qt::TabFocus);
	const QString name = QString::fromUtf8(iconName);
	auto apply = [button, &theme, name] {
		button->setIcon(theme.icon(name.toUtf8().constData()));
		const int px = theme.metrics().iconPx;
		button->setIconSize(QSize(px, px));
	};
	apply();
	QObject::connect(&theme, &Theme::changed, button, apply);
	return button;
}

QFrame *makeSeparator(QWidget *parent)
{
	auto *line = new QFrame(parent);
	line->setObjectName(QStringLiteral("rdSeparator"));
	line->setFixedHeight(1);
	return line;
}

Banner::Banner(Theme &theme, QWidget *parent) : QFrame(parent), theme_(theme)
{
	setObjectName(QStringLiteral("rdBanner"));
	setProperty("rdTone", tone_);

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(8, 6, 8, 6);
	layout->setSpacing(8);

	icon_ = new QLabel(this);
	icon_->setAlignment(Qt::AlignTop);
	layout->addWidget(icon_, 0, Qt::AlignTop);

	label_ = new QLabel(this);
	label_->setWordWrap(true);
	label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(label_, 1);

	connect(&theme_, &Theme::changed, this, &Banner::updateIcon);
	updateIcon();
}

void Banner::updateIcon()
{
	const char *name = "info";
	Theme::Tone tone = Theme::Tone::Muted;
	if (tone_ == QLatin1String("ok")) {
		name = "circle-check";
		tone = Theme::Tone::Ok;
	} else if (tone_ == QLatin1String("warning")) {
		name = "triangle-alert";
		tone = Theme::Tone::Warning;
	} else if (tone_ == QLatin1String("error")) {
		name = "circle-x";
		tone = Theme::Tone::Error;
	}
	const int px = theme_.metrics().iconPx;
	icon_->setPixmap(theme_.pixmap(name, theme_.color(tone), px, devicePixelRatioF()));
}

void Banner::setMessage(const char *tone, const QString &text)
{
	tone_ = QString::fromUtf8(tone);
	setStyleProperty(this, "rdTone", tone_);
	label_->setText(text);
	updateIcon();
}

void Banner::setMessage(const char *tone, const UserMessage &message)
{
	// The first sentence says what happened. The rest follows on its own lines.
	QString text = qs(message.what);
	if (!message.detail.empty())
		text += QStringLiteral("\n") + qs(message.detail);
	if (!message.action.empty())
		text += QStringLiteral("\n") + qs(message.action);
	setMessage(tone, text);
}

void Banner::setAction(const QString &label)
{
	if (!action_) {
		action_ = new QPushButton(this);
		layout()->addWidget(action_);
		connect(action_, &QPushButton::clicked, this, &Banner::actionClicked);
	}
	action_->setText(label);
	action_->setVisible(!label.isEmpty());
}

namespace {

bool g_logosEnabled = true;

// A logo from resources/brands, drawn in one colour. An empty picture when there is no such file.
QPixmap brandGlyph(const std::string &name, const QColor &color, int pixels)
{
	static QHash<QString, QPixmap> cache;
	const QString key = QStringLiteral("%1|%2|%3").arg(QString::fromStdString(name), color.name()).arg(pixels);
	const auto cached = cache.constFind(key);
	if (cached != cache.constEnd())
		return *cached;

	QPixmap result;
	QFile file(QStringLiteral(":/relaydock/brands/%1.svg").arg(QString::fromStdString(name)));
	if (!name.empty() && file.open(QIODevice::ReadOnly)) {
		QByteArray svg = file.readAll();
		svg.replace("currentColor", color.name().toUtf8());
		QBuffer buffer(&svg);
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer, "svg");
		reader.setScaledSize(QSize(pixels, pixels));
		const QImage image = reader.read();
		if (!image.isNull())
			result = QPixmap::fromImage(image);
	}
	cache.insert(key, result);
	return result;
}

bool hasLogo(const ProviderInfo &info)
{
	return !info.logo.empty() && QFile::exists(QStringLiteral(":/relaydock/brands/%1.svg").arg(QString::fromStdString(info.logo)));
}

} // namespace

QPixmap providerBadgePixmap(const ProviderInfo &info, bool logos, int sizePx, qreal devicePixelRatio, const QFont &baseFont)
{
	const int pixels = std::max(1, static_cast<int>(sizePx * devicePixelRatio + 0.5));
	QPixmap pixmap(pixels, pixels);
	pixmap.setDevicePixelRatio(devicePixelRatio);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	const qreal radius = sizePx * 0.22;
	const QRectF tile(0, 0, sizePx, sizePx);

	if (logos && hasLogo(info)) {
		const QColor background(qs(info.logoBackground));
		const QColor ink(qs(info.logoColor));
		painter.setPen(Qt::NoPen);
		painter.setBrush(background.isValid() ? background : QColor(Qt::white));
		painter.drawRoundedRect(tile, radius, radius);
		// A thin edge keeps a white tile visible on a light window and a black one on a dark one.
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(QColor(127, 127, 127, 110), 1));
		painter.drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

		// The logo keeps a clear margin, and stays at 16 pixels or more where the badge allows.
		int glyph = static_cast<int>(sizePx * 0.66 + 0.5);
		glyph = std::min(std::max(glyph, 16), std::max(8, sizePx - 4));
		const int glyphPixels = std::max(1, static_cast<int>(glyph * devicePixelRatio + 0.5));
		const QPixmap logo = brandGlyph(info.logo, ink.isValid() ? ink : QColor(Qt::black), glyphPixels);
		const qreal offset = (sizePx - glyph) / 2.0;
		painter.drawPixmap(QRectF(offset, offset, glyph, glyph), logo, QRectF(0, 0, glyphPixels, glyphPixels));
		return pixmap;
	}

	QColor color(qs(info.accentColor));
	if (!color.isValid())
		color = QColor(0x60, 0x60, 0x60);
	painter.setPen(Qt::NoPen);
	painter.setBrush(color);
	painter.drawRoundedRect(tile, radius, radius);

	const QString letters = info.monogram.empty() ? QStringLiteral("?") : qs(info.monogram);
	QFont font = baseFont;
	font.setBold(true);
	font.setPixelSize(std::max(8, static_cast<int>(sizePx * (letters.size() > 1 ? 0.40 : 0.50))));
	painter.setFont(font);
	painter.setPen(toQColor(readableOn(fromQColor(color))));
	painter.drawText(tile, Qt::AlignCenter, letters);
	return pixmap;
}

ProviderBadge::ProviderBadge(QWidget *parent) : QWidget(parent)
{
	setFixedSize(size_, size_);
	setUnknown();
}

void ProviderBadge::setProvider(const ProviderInfo &info)
{
	info_ = info;
	setAccessibleName(qs(info.displayName));
	update();
}

void ProviderBadge::setUnknown()
{
	info_ = ProviderInfo{};
	info_.monogram = "?";
	info_.accentColor = "#606060";
	update();
}

bool ProviderBadge::showsLogo() const
{
	return g_logosEnabled && hasLogo(info_);
}

void ProviderBadge::setLogosEnabled(bool enabled)
{
	g_logosEnabled = enabled;
}

bool ProviderBadge::logosEnabled()
{
	return g_logosEnabled;
}

void ProviderBadge::setBadgeSize(int px)
{
	size_ = std::max(16, px);
	setFixedSize(size_, size_);
	updateGeometry();
	update();
}

void ProviderBadge::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.drawPixmap(0, 0, providerBadgePixmap(info_, g_logosEnabled, size_, devicePixelRatioF(), font()));
}

ElidedLabel::ElidedLabel(QWidget *parent) : QLabel(parent)
{
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setTextFormat(Qt::PlainText);
}

void ElidedLabel::setFullText(const QString &text)
{
	if (text == full_)
		return;
	full_ = text;
	setAccessibleName(text);
	updateGeometry();
	update();
}

QSize ElidedLabel::minimumSizeHint() const
{
	return QSize(fontMetrics().horizontalAdvance(QStringLiteral("...")), fontMetrics().height());
}

QSize ElidedLabel::sizeHint() const
{
	return QSize(fontMetrics().horizontalAdvance(full_) + 2, fontMetrics().height());
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	QStyleOption option;
	option.initFrom(this);
	const QString shown = fontMetrics().elidedText(full_, Qt::ElideRight, width());
	// Explain what was cut off.
	setToolTip(shown == full_ ? QString() : full_);
	style()->drawItemText(&painter, rect(), Qt::AlignLeft | Qt::AlignVCenter, option.palette, isEnabled(), shown,
			      foregroundRole());
}

Pill::Pill(QWidget *parent) : QLabel(parent)
{
	setObjectName(QStringLiteral("rdPill"));
	setProperty("rdTone", QStringLiteral("neutral"));
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Pill::setStatus(const char *tone, const QString &text)
{
	setText(text);
	setStyleProperty(this, "rdTone", QString::fromUtf8(tone));
}

} // namespace rd
