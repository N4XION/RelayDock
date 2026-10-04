// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "security/secret_string.h"

#include <QObject>
#include <QTimer>

namespace rd {

// Copy Key: puts a stream key on the Windows clipboard with as little exposure as Windows
// allows.
//
//   - The clipboard entry is marked so Windows leaves it out of clipboard history (Win+V) and
//     does not sync it to other devices.
//   - After 30 seconds the clipboard is cleared, but only if it still holds the key. Something
//     the user copied in the meantime is left alone.
//   - RelayDock keeps no copy of the key for this. To check whether the clipboard still holds
//     the key it compares a hash that is salted for this one copy.
//
// Other programs can read the clipboard while the key is on it. Nothing can prevent that.
//
// Thread ownership: OBS UI thread.
class KeyClipboard : public QObject {
	Q_OBJECT

public:
	static constexpr int kClearAfterMs = 30000;

	explicit KeyClipboard(QObject *parent = nullptr);

	// Returns false when Windows did not accept the clipboard data.
	bool copy(const SecretString &secret);

	// Clears the clipboard now if it still holds the last copied key.
	void clearIfOurs();

	bool pending() const { return timer_.isActive(); }

Q_SIGNALS:
	// The key left the clipboard, by the timer or because the user copied something else.
	void cleared();

private:
	QByteArray fingerprint(const QString &text) const;

	QTimer timer_;
	QByteArray salt_;
	QByteArray expected_;
};

} // namespace rd
