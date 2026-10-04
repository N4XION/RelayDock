// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/key_clipboard.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QMimeData>
#include <QRandomGenerator>

namespace rd {

namespace {

// Qt maps a MIME type of this form to the Windows clipboard format named in `value`.
QString windowsFormat(const char *name)
{
	return QStringLiteral("application/x-qt-windows-mime;value=\"%1\"").arg(QString::fromLatin1(name));
}

QByteArray dwordZero()
{
	return QByteArray(4, '\0');
}

} // namespace

KeyClipboard::KeyClipboard(QObject *parent) : QObject(parent)
{
	timer_.setSingleShot(true);
	timer_.setInterval(kClearAfterMs);
	connect(&timer_, &QTimer::timeout, this, &KeyClipboard::clearIfOurs);
}

QByteArray KeyClipboard::fingerprint(const QString &text) const
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(salt_);
	hash.addData(text.toUtf8());
	return hash.result();
}

bool KeyClipboard::copy(const SecretString &secret)
{
	QClipboard *clipboard = QApplication::clipboard();
	if (!clipboard || secret.empty())
		return false;

	auto *data = new QMimeData();
	data->setText(QString::fromStdString(secret.reveal()));
	// Documented by Microsoft under "Clipboard Formats", cloud clipboard and history formats.
	data->setData(windowsFormat("ExcludeClipboardContentFromMonitorProcessing"), dwordZero());
	data->setData(windowsFormat("CanIncludeInClipboardHistory"), dwordZero());
	data->setData(windowsFormat("CanUploadToCloudClipboard"), dwordZero());
	clipboard->setMimeData(data); // The clipboard owns `data` now

	// A fresh salt each time, so the stored hash says nothing about the key by itself.
	salt_.resize(16);
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt_.data()), 4);
	expected_ = fingerprint(clipboard->text());
	timer_.start();
	return !clipboard->text().isEmpty();
}

void KeyClipboard::clearIfOurs()
{
	timer_.stop();
	if (expected_.isEmpty())
		return;

	QClipboard *clipboard = QApplication::clipboard();
	if (clipboard && fingerprint(clipboard->text()) == expected_)
		clipboard->clear();
	expected_.clear();
	salt_.clear();
	Q_EMIT cleared();
}

} // namespace rd
