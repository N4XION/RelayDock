// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "diagnostics/preflight.h"
#include "ui/ui_common.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace rd {

// The result of the pre-stream check: READY, WARNING or FAILED, and every finding with what
// it means and what to do.
//
// With `offerStart` the dialog doubles as the gate in front of Start All Enabled. It returns
// Accepted when the user chooses to start. A FAILED result cannot be started from here.
class PreflightDialog : public QDialog {
	Q_OBJECT

public:
	PreflightDialog(UiHost &host, PreflightReport report, bool offerStart, QWidget *parent = nullptr);

private:
	void showReport();

	UiHost &host_;
	PreflightReport report_;
	bool offerStart_;

	Pill *status_;
	QLabel *summary_;
	QVBoxLayout *items_;
	QPushButton *start_;
};

} // namespace rd
