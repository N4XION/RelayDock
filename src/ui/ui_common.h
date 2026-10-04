// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"
#include "settings/theme.h"
#include "utils/i18n.h"

#include <QColor>
#include <QFrame>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include <string>
#include <vector>

class QPushButton;

namespace rd {

class AppContext;
class KeyClipboard;
class Theme;

// What cards and dialogs may ask of the dock that owns them.
class UiHost {
public:
	virtual ~UiHost() = default;

	virtual AppContext &app() = 0;
	virtual Theme &theme() = 0;
	virtual KeyClipboard &clipboard() = 0;
	// The window dialogs open over: the OBS main window.
	virtual QWidget *dialogParent() = 0;

	virtual void editDestination(const std::string &id) = 0;
	virtual void addDestination(const std::string &providerId) = 0;
	// Opens the settings window on a page. An empty id keeps the last page.
	virtual void openSettings(const std::string &pageId) = 0;
	virtual void openVerticalEditor(const std::string &layoutId) = 0;
	// Shows one legal document, or the first-run review when the id is empty.
	virtual void showLegal(const std::string &documentId) = 0;
	virtual void runPreflight(bool startAfterwards) = 0;
};

// ---- Text ----------------------------------------------------------------------------------------

inline QString qs(const std::string &text)
{
	return QString::fromStdString(text);
}

inline std::string ss(const QString &text)
{
	return text.toStdString();
}

// A translated interface string. See utils/i18n.h for how keys and English text work.
inline QString uiText(const char *key, const char *english)
{
	return QString::fromStdString(loc(key, english));
}

template <class... Args> QString uiTextF(const char *key, const char *english, Args &&...args)
{
	return QString::fromStdString(locf(key, english, std::forward<Args>(args)...));
}

template <class... Args>
QString uiTextN(long long count, const char *keyOne, const char *englishOne, const char *keyMany, const char *englishMany,
		Args &&...args)
{
	return QString::fromStdString(locn(count, keyOne, englishOne, keyMany, englishMany, std::forward<Args>(args)...));
}

QColor toQColor(Rgb color);
Rgb fromQColor(const QColor &color);

// ---- Theme ---------------------------------------------------------------------------------------

// Holds the colours, sizes and style sheet RelayDock's windows use, and keeps every attached
// window up to date when the appearance settings or the OBS theme change.
//
// Thread ownership: OBS UI thread.
class Theme : public QObject {
	Q_OBJECT

public:
	explicit Theme(AppContext &app, QObject *parent = nullptr);

	// Recomputes everything from the settings and the current OBS palette.
	void refresh();

	// Styles a top-level RelayDock widget now and whenever the theme changes.
	void attach(QWidget *root);

	const ThemeColors &colors() const { return colors_; }
	const ThemeMetrics &metrics() const { return metrics_; }
	bool animations() const;

	enum class Tone { Text, Muted, Accent, AccentText, Ok, Warning, Error };
	QColor color(Tone tone) const;

	// A Lucide icon drawn in the given colour, sharp at any screen scale.
	QIcon icon(const char *name, Tone tone = Tone::Text);
	QPixmap pixmap(const char *name, const QColor &color, int sizePx, qreal devicePixelRatio);

Q_SIGNALS:
	void changed();

private:
	AppContext &app_;
	ThemeColors colors_;
	ThemeMetrics metrics_;
	QString styleSheet_;
	std::vector<QPointer<QWidget>> roots_;
	QHash<QString, QPixmap> pixmapCache_;
};

// ---- Small widgets -------------------------------------------------------------------------------

// Sets a dynamic property the style sheet selects on, and makes Qt apply the new style.
void setStyleProperty(QWidget *widget, const char *name, const QString &value);

QLabel *makeLabel(const QString &text, const char *objectName = nullptr, QWidget *parent = nullptr);

// An icon-only button. The tooltip doubles as the accessible name.
QToolButton *makeToolButton(Theme &theme, const char *iconName, const QString &tooltip, QWidget *parent = nullptr);

QFrame *makeSeparator(QWidget *parent = nullptr);

// A coloured strip with an icon and a message. Tones: "ok", "warning", "error", "neutral".
class Banner : public QFrame {
	Q_OBJECT

public:
	explicit Banner(Theme &theme, QWidget *parent = nullptr);

	void setMessage(const char *tone, const QString &text);
	void setMessage(const char *tone, const UserMessage &message);
	// Optional button at the right edge. Emits actionClicked.
	void setAction(const QString &label);
	QString text() const { return label_->text(); }

Q_SIGNALS:
	void actionClicked();

private:
	void updateIcon();

	Theme &theme_;
	QLabel *icon_;
	QLabel *label_;
	QPushButton *action_ = nullptr;
	QString tone_ = QStringLiteral("neutral");
};

// The square badge with a platform's initials. RelayDock ships no platform logos.
class ProviderBadge : public QWidget {
	Q_OBJECT

public:
	explicit ProviderBadge(QWidget *parent = nullptr);

	void setProvider(const QString &monogram, const QString &colorHex);
	void setBadgeSize(int px);

	QSize sizeHint() const override { return QSize(size_, size_); }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString monogram_;
	QColor color_{0x60, 0x60, 0x60};
	int size_ = 28;
};

// A one-line label that shortens its text with "..." when the dock is narrow. The full text
// is available as a tooltip.
class ElidedLabel : public QLabel {
	Q_OBJECT

public:
	explicit ElidedLabel(QWidget *parent = nullptr);
	void setFullText(const QString &text);
	QString fullText() const { return full_; }

	QSize minimumSizeHint() const override;
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString full_;
};

// A status label such as LIVE. Tones as for Banner.
class Pill : public QLabel {
	Q_OBJECT

public:
	explicit Pill(QWidget *parent = nullptr);
	void setStatus(const char *tone, const QString &text);
};

} // namespace rd
