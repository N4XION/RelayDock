// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/legal_dialog.h"

#include "app/app_context.h"
#include "build_info.h"
#include "legal/legal_documents.h"
#include "utils/clock.h"
#include "utils/log.h"

#include <QCheckBox>
#include <QFile>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace rd {

QString LegalDialog::loadDocument(const std::string &file)
{
	QFile resource(QStringLiteral(":/relaydock/legal/%1").arg(qs(file)));
	if (!resource.open(QIODevice::ReadOnly))
		return QString();
	return QString::fromUtf8(resource.readAll());
}

LegalDialog::LegalDialog(UiHost &host, Mode mode, const std::string &documentId, QWidget *parent)
	: QDialog(parent), host_(host), mode_(mode)
{
	setObjectName(QStringLiteral("rdDialog"));
	setWindowTitle(mode_ == Mode::Review ? uiText("Legal.Title.Review", "RelayDock: before your first stream")
					     : uiText("Legal.Title.View", "RelayDock legal documents"));
	resize(680, 640);

	if (mode_ == Mode::Review)
		documents_ = pendingLegalDocuments(host_.app().config().legal);
	else
		documents_ = {documentId};
	reachedEnd_.assign(documents_.size(), false);
	acknowledged_.assign(documents_.size(), false);

	auto *layout = new QVBoxLayout(this);
	progress_ = makeLabel(QString(), "rdSmall", this);
	title_ = makeLabel(QString(), "rdHeading", this);
	layout->addWidget(progress_);
	layout->addWidget(title_);

	text_ = new QTextBrowser(this);
	text_->setOpenExternalLinks(false);
	text_->setOpenLinks(false);
	text_->setAccessibleName(uiText("Legal.Text.Name", "Document text"));
	layout->addWidget(text_, 1);

	scrollHint_ = makeLabel(uiText("Legal.ScrollHint", "Scroll to the end of the document to continue."), "rdSmall", this);
	layout->addWidget(scrollHint_);

	acknowledge_ = new QCheckBox(this);
	layout->addWidget(acknowledge_);

	auto *buttons = new QHBoxLayout();
	back_ = new QPushButton(uiText("Legal.Back", "Back"), this);
	next_ = new QPushButton(this);
	next_->setObjectName(QStringLiteral("rdPrimary"));
	auto *close = new QPushButton(mode_ == Mode::Review ? uiText("Legal.Later", "Not now") : uiText("Common.Close", "Close"), this);
	buttons->addWidget(close);
	buttons->addStretch(1);
	buttons->addWidget(back_);
	buttons->addWidget(next_);
	layout->addLayout(buttons);

	if (mode_ == Mode::View) {
		progress_->hide();
		scrollHint_->hide();
		acknowledge_->hide();
		back_->hide();
		next_->hide();
	}

	connect(close, &QPushButton::clicked, this, &QDialog::reject);
	connect(back_, &QPushButton::clicked, this, [this] { showPage(index_ - 1); });
	connect(next_, &QPushButton::clicked, this, [this] {
		if (index_ + 1 < static_cast<int>(documents_.size()))
			showPage(index_ + 1);
		else
			finish();
	});
	connect(acknowledge_, &QCheckBox::toggled, this, [this](bool checked) {
		if (index_ < static_cast<int>(acknowledged_.size()))
			acknowledged_[static_cast<size_t>(index_)] = checked;
		updateControls();
	});

	connect(text_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { checkEnd(false); });

	host_.theme().attach(this);
	if (!documents_.empty())
		showPage(0);
}

// The end counts as reached when the scroll bar is at its maximum. While a text loads, the
// maximum is 0 for a moment. That must not count, or the box would unlock without any
// scrolling. So a maximum of 0 only counts in the delayed check, when the size is final.
void LegalDialog::checkEnd(bool settled)
{
	if (mode_ != Mode::Review || !isVisible() || index_ >= static_cast<int>(reachedEnd_.size()))
		return;
	const QScrollBar *bar = text_->verticalScrollBar();
	const bool needsNoScrolling = bar->maximum() <= 2;
	const bool atEnd = needsNoScrolling ? settled : bar->value() >= bar->maximum() - 2;
	if (atEnd && !reachedEnd_[static_cast<size_t>(index_)]) {
		reachedEnd_[static_cast<size_t>(index_)] = true;
		updateControls();
	}
}

void LegalDialog::scheduleEndCheck()
{
	const int page = index_;
	QTimer::singleShot(250, this, [this, page] {
		if (page == index_)
			checkEnd(true);
	});
}

void LegalDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	scheduleEndCheck();
}

void LegalDialog::showPage(int index)
{
	if (index < 0 || index >= static_cast<int>(documents_.size()))
		return;
	index_ = index;

	const LegalDocument *document = findLegalDocument(documents_[static_cast<size_t>(index)]);
	if (!document) {
		title_->setText(uiText("Legal.Missing.Title", "Document not found"));
		text_->setPlainText(uiText("Legal.Missing", "This RelayDock version does not contain that document."));
		return;
	}

	progress_->setText(uiTextF("Legal.Progress", "Document {0} of {1}", index + 1, documents_.size()));
	title_->setText(qs(document->title));

	const QString markdown = loadDocument(document->file);
	if (markdown.isEmpty()) {
		// Never let someone agree to a text that did not load.
		text_->setPlainText(uiTextF("Legal.LoadFailed",
					    "RelayDock could not load {0}. Reinstall RelayDock. You cannot continue without the text.",
					    document->file));
		acknowledge_->setEnabled(false);
		next_->setEnabled(false);
		return;
	}
	text_->setMarkdown(markdown);
	text_->verticalScrollBar()->setValue(0);

	const QSignalBlocker blocker(acknowledge_);
	acknowledge_->setText(qs(legalAcknowledgement(*document)));
	acknowledge_->setChecked(acknowledged_[static_cast<size_t>(index)]);
	updateControls();
	// A document short enough to fit needs no scrolling. That is only known after layout.
	scheduleEndCheck();
}

void LegalDialog::updateControls()
{
	if (mode_ != Mode::Review || documents_.empty())
		return;
	const size_t i = static_cast<size_t>(index_);
	const bool last = index_ + 1 == static_cast<int>(documents_.size());

	acknowledge_->setEnabled(reachedEnd_[i]);
	scrollHint_->setVisible(!reachedEnd_[i]);
	back_->setEnabled(index_ > 0);
	next_->setText(last ? uiText("Legal.Finish", "Finish") : uiText("Legal.Continue", "Continue"));
	next_->setEnabled(reachedEnd_[i] && acknowledged_[i]);
}

void LegalDialog::finish()
{
	// Every page must have been read to its end and acknowledged.
	for (size_t i = 0; i < documents_.size(); ++i) {
		if (!reachedEnd_[i] || !acknowledged_[i]) {
			showPage(static_cast<int>(i));
			return;
		}
	}

	AppContext &app = host_.app();
	const std::string now = utcTimestampIso8601();
	const std::string version = buildInfo().version;
	for (const std::string &id : documents_)
		recordLegalAcceptance(app.config().legal, id, now, version);
	pruneLegalRecords(app.config().legal);
	app.notifyConfigChanged();
	logInfo("Legal documents reviewed and accepted: {} document(s).", documents_.size());
	accept();
}

} // namespace rd
