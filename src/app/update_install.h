// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/user_message.h"
#include "update/update_download.h"

#include <QObject>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace rd {

// "Update now", from the click to the moment OBS closes.
//
// The user chooses Update now. RelayDock downloads the installer of the newer release and
// checks it (update/update_download.h), then starts it. The installer waits, without a window,
// until OBS has closed, and then installs. RelayDock makes a request file for it, as it does
// for the uninstall: the installer goes ahead only while that file exists. Deleting the file is
// how the user changes their mind, at any moment before OBS closes.
//
// Nothing here starts by itself. Without the click there is no download.
//
// When OBS shuts down, a download that still runs ends and nothing is installed. An installer
// that already waits goes ahead.
//
// Thread ownership: OBS UI thread. The download runs on a thread of its own and reports back
// to the UI thread.
class UpdateInstall : public QObject {
	Q_OBJECT

public:
	enum class State {
		Idle,
		Downloading,
		Waiting, // The installer runs and waits for OBS to close
		Failed,
	};

	struct Setup {
		DownloadRules rules;
		std::string userAgent = "RelayDock";
		// The folder the installer put this RelayDock into. Empty for a RelayDock that was
		// copied by hand, which Update now cannot serve.
		std::string installFolder;
		// The folder downloads go into, each into a new folder of its own. The temp folder.
		std::string downloadFolder;
		// Added to the installer's command line. A test build uses it.
		std::string extraArguments;
	};

	explicit UpdateInstall(QObject *parent = nullptr);
	~UpdateInstall() override;

	// Call once, before anything else. Also removes what an earlier update left in the
	// download folder.
	void configure(Setup setup);

	// Whether the installer put this RelayDock here. Only such a copy can update itself.
	bool installedByInstaller() const { return !setup_.installFolder.empty(); }
	// Whether Update now can serve this release with this RelayDock.
	bool possible(const ReleaseInfo &offered) const;

	// Begins the download. Returns false when it cannot, with the reason in problem().
	bool start(const ReleaseInfo &offered);
	// Downloading: ends the download. Waiting: takes the request back, and the installer ends
	// without installing, whether OBS closes a second later or an hour later.
	void cancel();
	// OBS is closing.
	void shutdown();

	State state() const { return state_; }
	bool busy() const { return state_ == State::Downloading || state_ == State::Waiting; }
	// The release the last start() was for.
	const ReleaseInfo &release() const { return release_; }
	// Failed: what went wrong and what to do.
	const UserMessage &problem() const { return problem_; }
	// Downloading: bytes so far, and bytes expected, or 0 when nobody said how many.
	uint64_t received() const { return received_; }
	uint64_t total() const { return total_; }
	// Waiting: the file whose removal takes the request back, and the installer that runs.
	const std::string &requestFile() const { return requestFile_; }
	const std::string &installerFile() const { return installerFile_; }

Q_SIGNALS:
	void changed();    // The state changed
	void progressed(); // More of the download arrived

private:
	void endDownload();
	void finishDownload(UpdateDownload download);
	void fail(UserMessage message);

	Setup setup_;
	State state_ = State::Idle;
	ReleaseInfo release_;
	UserMessage problem_;
	uint64_t received_ = 0;
	uint64_t total_ = 0;
	std::string requestFile_;
	std::string installerFile_;
	std::thread thread_;
	std::shared_ptr<std::atomic<bool>> cancel_;
};

} // namespace rd
