// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/types.h"
#include "encoders/encoder_caps.h"
#include "security/secret_string.h"
#include "ui/ui_common.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace rd {

class ConnectionTester;
class IProvider;

// A stream key or password field that never shows a saved secret.
//
//   Saved    Dots and three buttons: Replace, Copy, Remove.
//   Entry    A password box. What you type shows as dots.
//   Removal  A note that the secret goes away when you save, with Undo.
//
// The field changes nothing by itself. The dialog reads its decision when you choose Save.
class SecretField : public QWidget {
	Q_OBJECT

public:
	enum class Action { Keep, Replace, Remove };

	SecretField(UiHost &host, const QString &what, QWidget *parent = nullptr);

	// `saved` says whether the credential store holds a value right now.
	void setSaved(bool saved);
	// Called to fetch the saved secret for Copy. The field never keeps it.
	std::function<bool(SecretString &)> fetch;

	Action action() const;
	// The typed value. Only meaningful when action() is Replace.
	SecretString typed() const;
	// Whether a secret exists after saving, for validation.
	bool willHaveSecret() const;

Q_SIGNALS:
	void changed();

private:
	void showState();

	UiHost &host_;
	QString what_;
	bool saved_ = false;
	bool removing_ = false;
	bool entering_ = false;

	QLabel *mask_;
	QLabel *note_;
	QLineEdit *entry_;
	QPushButton *replace_;
	QPushButton *copy_;
	QPushButton *remove_;
	QPushButton *undo_;
};

// Adds or edits one destination. Works on a copy. Nothing changes until Save.
class DestinationDialog : public QDialog {
	Q_OBJECT

public:
	DestinationDialog(UiHost &host, DestinationConfig config, bool isNew, QWidget *parent = nullptr);

	void accept() override;

private:
	QWidget *buildConnectionPage();
	QWidget *buildVideoPage();
	QWidget *buildAudioPage();
	QWidget *buildAdvancedPage();

	void load();
	void store();
	// Shows only the controls the chosen encoder really has.
	void updateEncoderControls();
	void updateServerControls();
	void updateFeedback();
	void testConnection();
	const VideoEncoderCaps *currentCaps(const StreamContext &context) const;

	UiHost &host_;
	DestinationConfig working_;
	bool isNew_;
	const IProvider *provider_;
	bool loading_ = true;

	QTabWidget *tabs_;
	Banner *liveNote_;
	Banner *feedback_;
	QLabel *effective_;

	// Connection
	QLineEdit *name_;
	QComboBox *server_;
	QLabel *serverUrlLabel_;
	QLineEdit *serverUrl_;
	SecretField *key_;
	QCheckBox *useAuth_;
	QLabel *usernameLabel_;
	QLineEdit *username_;
	QLabel *passwordLabel_;
	SecretField *password_;
	QPushButton *testButton_;
	Banner *testResult_;
	ConnectionTester *tester_ = nullptr;

	// Video
	QComboBox *orientation_;
	QLabel *layoutLabel_;
	QWidget *layoutRow_;
	QComboBox *layout_;
	QComboBox *resolution_;
	QCheckBox *lockResolution_;
	QComboBox *fps_;
	QCheckBox *lockFps_;
	QSpinBox *bitrate_;
	QCheckBox *lockBitrate_;
	QComboBox *encoder_;
	QCheckBox *lockEncoder_;
	QFormLayout *videoForm_;
	QComboBox *rateControl_;
	QComboBox *preset_;
	QComboBox *profile_;
	QSpinBox *keyframe_;
	QSpinBox *bFrames_;
	QLineEdit *customOptions_;
	QLabel *lockNote_;

	// Audio
	QComboBox *audioBitrate_;
	QComboBox *audioEncoder_;
	QSpinBox *audioTrack_;

	// Advanced
	QCheckBox *autoReconnect_;
	QSpinBox *reconnectAttempts_;
	QSpinBox *reconnectDelay_;
	QSpinBox *streamDelay_;
	QLineEdit *bindIp_;
	QCheckBox *autoOptimize_;
};

} // namespace rd
