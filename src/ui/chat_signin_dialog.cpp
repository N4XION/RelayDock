// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/chat_signin_dialog.h"

#include "app/app_context.h"
#include "app/chat_hub.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace rd {

namespace {

// Only ever a page of twitch.tv. The reader of Twitch's answer refuses anything else, and this
// checks again before a browser opens.
bool isTwitchPage(const QString &address)
{
	const QUrl url(address);
	return url.scheme() == QLatin1String("https") &&
	       (url.host() == QLatin1String("www.twitch.tv") || url.host() == QLatin1String("twitch.tv"));
}

} // namespace

ChatSignInDialog::ChatSignInDialog(UiHost &host, QWidget *parent) : QDialog(parent), host_(host)
{
	setObjectName(QStringLiteral("rdDialog"));
	setWindowTitle(uiText("ChatSignIn.Title", "Sign in to Twitch"));
	setMinimumWidth(460);

	auto *layout = new QVBoxLayout(this);
	layout->setSpacing(10);
	layout->addWidget(makeLabel(uiText("ChatSignIn.Headline", "Let RelayDock read your Twitch chat"), "rdTitle", this));

	// One label per line, so no text carries a line break of its own.
	const QString lines[] = {
		uiText("ChatSignIn.Step1", "1. Choose Open twitch.tv. Your browser opens a page of Twitch."),
		uiText("ChatSignIn.Step2", "2. Check that the page shows the code below. Type it in if the page asks."),
		uiText("ChatSignIn.Step3", "3. Choose Authorize on that page. RelayDock notices by itself."),
	};
	auto *steps = new QVBoxLayout();
	steps->setSpacing(2);
	for (const QString &line : lines) {
		QLabel *label = makeLabel(line, nullptr, this);
		label->setWordWrap(true);
		steps->addWidget(label);
	}
	layout->addLayout(steps);

	code_ = makeLabel(QString(), "rdHeading", this);
	code_->setAccessibleName(uiText("ChatSignIn.Code.Name", "Sign-in code"));
	code_->setAlignment(Qt::AlignCenter);
	code_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	QFont codeFont = code_->font();
	codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 3);
	codeFont.setPointSize(codeFont.pointSize() + 6);
	code_->setFont(codeFont);
	layout->addWidget(code_);

	state_ = makeLabel(QString(), "rdMuted", this);
	state_->setWordWrap(true);
	state_->setAlignment(Qt::AlignCenter);
	layout->addWidget(state_);

	result_ = new Banner(host_.theme(), this);
	result_->hide();
	layout->addWidget(result_);

	QLabel *note = makeLabel(uiText("ChatSignIn.Note",
					"You type your Twitch password on twitch.tv only. RelayDock never sees it. RelayDock asks Twitch for one permission, to read chat, and keeps the sign-in in Windows Credential Manager."),
				 "rdSmall", this);
	note->setWordWrap(true);
	layout->addWidget(note);

	auto *buttons = new QHBoxLayout();
	copy_ = new QPushButton(uiText("ChatSignIn.Copy", "Copy code"), this);
	retry_ = new QPushButton(uiText("ChatSignIn.Retry", "Try again"), this);
	close_ = new QPushButton(uiText("ChatSignIn.Cancel", "Cancel"), this);
	open_ = new QPushButton(uiText("ChatSignIn.Open", "Open twitch.tv"), this);
	open_->setObjectName(QStringLiteral("rdPrimary"));
	open_->setDefault(true);
	buttons->addWidget(copy_);
	buttons->addStretch(1);
	buttons->addWidget(retry_);
	buttons->addWidget(close_);
	buttons->addWidget(open_);
	layout->addLayout(buttons);

	ChatHub &hub = host_.app().chat();
	connect(&hub, &ChatHub::twitchSignInCode, this, &ChatSignInDialog::showCode);
	connect(&hub, &ChatHub::twitchSignInFinished, this, &ChatSignInDialog::showResult);
	connect(open_, &QPushButton::clicked, this, [this] {
		if (isTwitchPage(page_))
			QDesktopServices::openUrl(QUrl(page_));
	});
	connect(copy_, &QPushButton::clicked, this, [this] {
		// The code is no secret: it is useless without the sign-in on twitch.tv.
		QApplication::clipboard()->setText(code_->text());
	});
	connect(retry_, &QPushButton::clicked, this, &ChatSignInDialog::begin);
	connect(close_, &QPushButton::clicked, this, &QDialog::reject);

	host_.theme().attach(this);
	begin();
}

ChatSignInDialog::~ChatSignInDialog()
{
	// Closing the window ends a sign-in that is still waiting.
	if (!done_)
		host_.app().chat().cancelTwitchSignIn();
}

void ChatSignInDialog::begin()
{
	done_ = false;
	page_.clear();
	code_->setText(QStringLiteral("- - - -"));
	state_->setText(uiText("ChatSignIn.Asking", "Asking Twitch for a code..."));
	state_->show();
	result_->hide();
	open_->setEnabled(false);
	copy_->setEnabled(false);
	retry_->hide();
	close_->setText(uiText("ChatSignIn.Cancel", "Cancel"));
	host_.app().chat().startTwitchSignIn();
}

void ChatSignInDialog::showCode(const QString &code, const QString &page)
{
	page_ = isTwitchPage(page) ? page : QString();
	code_->setText(code);
	state_->setText(uiText("ChatSignIn.Waiting", "Waiting for you to confirm on twitch.tv."));
	open_->setEnabled(!page_.isEmpty());
	copy_->setEnabled(true);
}

void ChatSignInDialog::showResult(bool ok, const QString &login, const UserMessage &problem)
{
	done_ = true;
	state_->hide();
	open_->setEnabled(false);
	copy_->setEnabled(false);
	if (ok) {
		result_->setMessage("ok", uiTextF("ChatSignIn.Done", "Signed in as {0}. RelayDock reads your Twitch chat now.", ss(login)));
		result_->show();
		close_->setText(uiText("ChatSignIn.Close", "Close"));
		close_->setDefault(true);
		// Nothing is left to do here.
		QTimer::singleShot(1500, this, &QDialog::accept);
		return;
	}
	result_->setMessage("error", problem);
	result_->show();
	retry_->show();
	close_->setText(uiText("ChatSignIn.Close", "Close"));
}

} // namespace rd
