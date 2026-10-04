// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/update_dialog.h"

#include "app/app_context.h"
#include "build_info.h"
#include "utils/log.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace rd {

namespace {

// Only ever an address on github.com. The parser drops anything else, and this checks again.
void openOnGitHub(const std::string &address)
{
	const QString url = qs(address);
	if (url.startsWith(QStringLiteral("https://github.com/")))
		QDesktopServices::openUrl(QUrl(url));
}

} // namespace

UpdateDialog::UpdateDialog(UiHost &host, const ReleaseInfo &release, QWidget *parent)
	: QDialog(parent),
	  host_(host),
	  release_(release)
{
	setObjectName(QStringLiteral("rdDialog"));
	setWindowTitle(uiText("Update.Title", "RelayDock update"));
	setMinimumWidth(460);

	const std::string offered = versionOfTag(release_.tag);
	const bool hasInstaller = !release_.installerUrl.empty();

	auto *layout = new QVBoxLayout(this);
	layout->setSpacing(10);

	layout->addWidget(makeLabel(uiTextF("Update.Headline", "RelayDock {0} is available", offered), "rdTitle", this));
	layout->addWidget(makeLabel(uiTextF("Update.Running", "You run {0}.", std::string(buildInfo().version)), "rdMuted", this));

	// One label per line, so no text carries a line break of its own.
	const QString lines[] = {
		uiText("Update.Steps", "To update:"),
		hasInstaller ? uiText("Update.Step1", "1. Choose Download installer. Your browser saves the file.")
			     : uiText("Update.Step1.Page", "1. Choose Open release page and download the installer there."),
		uiText("Update.Step2", "2. Close OBS Studio."),
		uiText("Update.Step3", "3. Open the downloaded file and follow its steps."),
		uiText("Update.Keeps", "Your destinations, settings and stream keys stay."),
	};
	auto *steps = new QVBoxLayout();
	steps->setSpacing(2);
	for (const QString &line : lines) {
		QLabel *label = makeLabel(line, nullptr, this);
		label->setWordWrap(true);
		steps->addWidget(label);
	}
	layout->addLayout(steps);

	QLabel *note = makeLabel(uiText("Update.Note",
					"RelayDock installs nothing by itself. It looks for a new version when OBS starts. Switch that off under Settings, Updates."),
				 "rdSmall", this);
	note->setWordWrap(true);
	layout->addWidget(note);

	auto *buttons = new QHBoxLayout();
	auto *skip = new QPushButton(uiText("Update.Skip", "Skip this version"), this);
	auto *notes = new QPushButton(uiText("Update.Notes", "What changed"), this);
	auto *later = new QPushButton(uiText("Update.Later", "Later"), this);
	auto *download = new QPushButton(hasInstaller ? uiText("Update.Download", "Download installer")
						      : uiText("Updates.Open", "Open release page"),
					 this);
	download->setObjectName(QStringLiteral("rdPrimary"));
	download->setDefault(true);
	notes->setVisible(hasInstaller && !release_.url.empty());
	buttons->addWidget(skip);
	buttons->addStretch(1);
	buttons->addWidget(notes);
	buttons->addWidget(later);
	buttons->addWidget(download);
	layout->addLayout(buttons);

	connect(download, &QPushButton::clicked, this, [this, hasInstaller] {
		openOnGitHub(hasInstaller ? release_.installerUrl : release_.url);
		accept();
	});
	connect(notes, &QPushButton::clicked, this, [this] { openOnGitHub(release_.url); });
	connect(later, &QPushButton::clicked, this, &QDialog::reject);
	connect(skip, &QPushButton::clicked, this, [this, offered] {
		AppContext &app = host_.app();
		app.config().general.skippedUpdateVersion = offered;
		app.notifyConfigChanged();
		logInfo("You chose to skip RelayDock {}. The check at start-up stays quiet until a newer version exists.", offered);
		reject();
	});

	host_.theme().attach(this);
}

} // namespace rd
