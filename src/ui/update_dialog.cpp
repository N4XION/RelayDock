// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/update_dialog.h"

#include "app/app_context.h"
#include "app/update_install.h"
#include "build_info.h"
#include "utils/log.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <format>

namespace rd {

namespace {

// Only ever an address on github.com. The parser drops anything else, and this checks again.
void openOnGitHub(const std::string &address)
{
	const QString url = qs(address);
	if (url.startsWith(QStringLiteral("https://github.com/")))
		QDesktopServices::openUrl(QUrl(url));
}

std::string megabytes(uint64_t bytes)
{
	return std::format("{:.1f}", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

} // namespace

UpdateDialog::UpdateDialog(UiHost &host, const ReleaseInfo &release, QWidget *parent)
	: QDialog(parent),
	  host_(host),
	  release_(release)
{
	setObjectName(QStringLiteral("rdDialog"));
	setWindowTitle(uiText("Update.Title", "RelayDock update"));

	UpdateInstall &install = host_.app().updateInstall();
	inPlace_ = install.possible(release_);
	const std::string offered = versionOfTag(release_.tag);
	const bool hasInstaller = !release_.installerUrl.empty();

	auto *layout = new QVBoxLayout(this);
	layout->setSpacing(10);
	// The window is as wide as its buttons need, and they change with what the update is doing.
	layout->setSizeConstraint(QLayout::SetMinimumSize);

	QLabel *headline = makeLabel(uiTextF("Update.Headline", "RelayDock {0} is available", offered), "rdTitle", this);
	headline->setMinimumWidth(436);
	layout->addWidget(headline);
	layout->addWidget(makeLabel(uiTextF("Update.Running", "You run {0}.", std::string(buildInfo().version)), "rdMuted", this));

	// One label per line, so no text carries a line break of its own.
	QStringList lines;
	if (inPlace_) {
		lines << uiText("UpdateNow.Steps", "Update now does this:")
		      << uiText("UpdateNow.Step1", "1. Downloads the installer from the release page on GitHub.")
		      << uiText("UpdateNow.Step2", "2. Checks it against the checksums of the release.")
		      << uiText("UpdateNow.Step3", "3. Installs it when you close OBS Studio. You can cancel until then.");
	} else {
		lines << uiText("Update.Steps", "To update:")
		      << (hasInstaller ? uiText("Update.Step1", "1. Choose Download installer. Your browser saves the file.")
				       : uiText("Update.Step1.Page", "1. Choose Open release page and download the installer there."))
		      << uiText("Update.Step2", "2. Close OBS Studio.") << uiText("Update.Step3", "3. Open the downloaded file and follow its steps.");
	}
	lines << uiText("Update.Keeps", "Your destinations, settings and stream keys stay.");

	steps_ = new QWidget(this);
	auto *steps = new QVBoxLayout(steps_);
	steps->setContentsMargins(0, 0, 0, 0);
	steps->setSpacing(2);
	for (const QString &line : lines) {
		QLabel *label = makeLabel(line, nullptr, steps_);
		label->setWordWrap(true);
		steps->addWidget(label);
	}
	layout->addWidget(steps_);

	progress_ = new QWidget(this);
	auto *progress = new QVBoxLayout(progress_);
	progress->setContentsMargins(0, 0, 0, 0);
	progress->setSpacing(6);
	progressText_ = makeLabel(QString(), nullptr, progress_);
	bar_ = new QProgressBar(progress_);
	bar_->setObjectName(QStringLiteral("rdMeter"));
	bar_->setTextVisible(false);
	bar_->setRange(0, 1000);
	progress->addWidget(progressText_);
	progress->addWidget(bar_);
	layout->addWidget(progress_);

	state_ = new Banner(host_.theme(), this);
	layout->addWidget(state_);

	QLabel *note = makeLabel(uiText("Update.Note",
					"RelayDock installs nothing by itself. It looks for a new version when OBS starts. Switch that off under Settings, Updates."),
				 "rdSmall", this);
	note->setWordWrap(true);
	layout->addWidget(note);

	auto *buttons = new QHBoxLayout();
	skip_ = new QPushButton(uiText("Update.Skip", "Skip this version"), this);
	notes_ = new QPushButton(uiText("Update.Notes", "What changed"), this);
	browser_ = new QPushButton(uiText("Update.Download", "Download installer"), this);
	later_ = new QPushButton(this);
	cancel_ = new QPushButton(this);
	primary_ = new QPushButton(this);
	primary_->setObjectName(QStringLiteral("rdPrimary"));
	primary_->setDefault(true);
	buttons->addWidget(skip_);
	buttons->addStretch(1);
	buttons->addWidget(notes_);
	buttons->addWidget(browser_);
	buttons->addWidget(cancel_);
	buttons->addWidget(later_);
	buttons->addWidget(primary_);
	layout->addLayout(buttons);

	connect(primary_, &QPushButton::clicked, this, &UpdateDialog::onPrimary);
	connect(notes_, &QPushButton::clicked, this, [this] { openOnGitHub(release_.url); });
	connect(browser_, &QPushButton::clicked, this, [this] { openOnGitHub(release_.installerUrl); });
	connect(later_, &QPushButton::clicked, this, &QDialog::reject);
	connect(cancel_, &QPushButton::clicked, this, [this] { host_.app().updateInstall().cancel(); });
	connect(skip_, &QPushButton::clicked, this, [this, offered] {
		AppContext &app = host_.app();
		app.config().general.skippedUpdateVersion = offered;
		app.notifyConfigChanged();
		logInfo("You chose to skip RelayDock {}. The check at start-up stays quiet until a newer version exists.", offered);
		reject();
	});
	connect(&install, &UpdateInstall::changed, this, &UpdateDialog::refresh);
	connect(&install, &UpdateInstall::progressed, this, &UpdateDialog::showProgress);

	refresh();
	host_.theme().attach(this);
}

int UpdateDialog::stage() const
{
	// An update of another release is not this window's business.
	const UpdateInstall &install = host_.app().updateInstall();
	const bool mine = install.release().tag == release_.tag;
	return static_cast<int>(mine ? install.state() : UpdateInstall::State::Idle);
}

void UpdateDialog::refresh()
{
	const UpdateInstall &install = host_.app().updateInstall();
	const auto state = static_cast<UpdateInstall::State>(stage());
	const bool idle = state == UpdateInstall::State::Idle;
	const bool downloading = state == UpdateInstall::State::Downloading;
	const bool waiting = state == UpdateInstall::State::Waiting;
	const bool failed = state == UpdateInstall::State::Failed;
	const bool hasInstaller = !release_.installerUrl.empty();
	const std::string offered = versionOfTag(release_.tag);

	steps_->setVisible(idle);
	progress_->setVisible(downloading);
	state_->setVisible(waiting || failed);
	if (waiting)
		state_->setMessage("ok", uiTextF("UpdateNow.Ready",
						 "RelayDock {0} is downloaded and checked. It installs when you close OBS Studio.", offered));
	else if (failed)
		state_->setMessage("error", install.problem());
	if (downloading)
		showProgress();

	skip_->setVisible(idle);
	notes_->setVisible(!release_.url.empty() && !downloading && !waiting && (inPlace_ || hasInstaller));
	// After a failed Update now, the browser is the other way to the same file.
	browser_->setVisible(failed && hasInstaller);
	later_->setVisible(!downloading);
	later_->setText(waiting ? uiText("Update.Close", "Close") : uiText("Update.Later", "Later"));
	cancel_->setVisible(downloading || waiting);
	cancel_->setText(downloading ? uiText("UpdateNow.Cancel", "Cancel") : uiText("UpdateNow.CancelUpdate", "Cancel update"));

	primary_->setVisible(!downloading);
	// Another update is under way. One at a time.
	primary_->setEnabled(!(idle && install.busy()));
	// One thing waits for OBS to close at a time, and the uninstall was asked for first.
	if (idle && inPlace_ && host_.app().uninstallRequest().pending()) {
		state_->setMessage("warning", uiText("UpdateNow.Leaving",
						     "RelayDock is set to be removed when you close OBS Studio. To update instead, choose Keep RelayDock under Settings, Updates first."));
		state_->setVisible(true);
		primary_->setEnabled(false);
	}
	if (waiting)
		primary_->setText(uiText("UpdateNow.CloseObs", "Close OBS and install"));
	else if (inPlace_)
		primary_->setText(failed ? uiText("UpdateNow.Retry", "Try again") : uiText("UpdateNow.Button", "Update now"));
	else
		primary_->setText(hasInstaller ? uiText("Update.Download", "Download installer") : uiText("Updates.Open", "Open release page"));
}

void UpdateDialog::showProgress()
{
	const UpdateInstall &install = host_.app().updateInstall();
	const uint64_t received = install.received();
	const uint64_t total = install.total();
	if (total > 0) {
		bar_->setRange(0, 1000);
		bar_->setValue(static_cast<int>(received >= total ? 1000 : received * 1000 / total));
		progressText_->setText(uiTextF("UpdateNow.Progress", "Downloading the installer: {0} of {1} MB", megabytes(received), megabytes(total)));
	} else {
		// Nobody said how large the file is. The bar moves without a measure.
		bar_->setRange(0, 0);
		progressText_->setText(uiTextF("UpdateNow.Progress.Unknown", "Downloading the installer: {0} MB", megabytes(received)));
	}
}

void UpdateDialog::onPrimary()
{
	UpdateInstall &install = host_.app().updateInstall();
	const auto state = static_cast<UpdateInstall::State>(stage());

	if (state == UpdateInstall::State::Waiting) {
		// Closing OBS is what lets the installer go ahead. OBS asks its own questions first,
		// for example while a stream runs, and so does RelayDock.
		QWidget *obs = host_.dialogParent();
		accept();
		if (obs)
			QTimer::singleShot(0, obs, [obs] { obs->window()->close(); });
		return;
	}

	if (inPlace_) {
		install.start(release_);
		return;
	}

	openOnGitHub(release_.installerUrl.empty() ? release_.url : release_.installerUrl);
	accept();
}

} // namespace rd
