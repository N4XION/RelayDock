// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "ui/ui_common.h"

#include <QDialog>

#include <string>
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;
class QTextBrowser;

namespace rd {

// Shows RelayDock's legal documents.
//
// Review mode is the first-run onboarding. It walks through every document the user has not
// yet accepted in its current version, one page per document. For each one:
//   - the acknowledgement box stays disabled until the text has been scrolled to its end,
//   - Continue stays disabled until the box is ticked.
// Nothing is recorded unless the user finishes the last page. Closing the window part-way
// leaves everything as it was.
//
// View mode shows one document to read again later. It records nothing.
class LegalDialog : public QDialog {
	Q_OBJECT

public:
	enum class Mode { Review, View };

	LegalDialog(UiHost &host, Mode mode, const std::string &documentId, QWidget *parent = nullptr);

	// Loads a document's text from the resources built into the plugin.
	static QString loadDocument(const std::string &file);

protected:
	void showEvent(QShowEvent *event) override;

private:
	void showPage(int index);
	// Marks the page as read to its end when the scroll bar is at the bottom. A text that
	// needs no scrolling only counts when `settled` is set, which the delayed check does once
	// the text has its final size.
	void checkEnd(bool settled);
	// Runs checkEnd once the text has been laid out, for a document that needs no scrolling.
	void scheduleEndCheck();
	void updateControls();
	void finish();

	UiHost &host_;
	Mode mode_;
	std::vector<std::string> documents_; // Ids, in the order shown
	std::vector<bool> reachedEnd_;
	std::vector<bool> acknowledged_;
	int index_ = 0;

	QLabel *progress_;
	QLabel *title_;
	QTextBrowser *text_;
	QLabel *scrollHint_;
	QCheckBox *acknowledge_;
	QPushButton *back_;
	QPushButton *next_;
};

} // namespace rd
