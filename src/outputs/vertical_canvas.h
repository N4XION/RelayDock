// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/vertical_layout.h"

#include <QObject>

#include <obs.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rd {

// One vertical canvas: a private OBS scene that holds a layout's items, and an OBS view that
// renders that scene at the canvas size so encoders can read it.
//
// Cost when idle is zero. The view only joins the OBS render loop while an encoder uses it
// (acquireVideo to releaseVideo). The editor preview draws the scene directly and needs no view.
//
// The scene holds references to OBS sources. They must be released when the scene collection
// changes and before OBS exits, which is what releaseSources() is for.
//
// Thread ownership: OBS UI thread.
class VerticalCanvas {
public:
	VerticalCanvas(std::string layoutId, int width, int height);
	~VerticalCanvas();

	VerticalCanvas(const VerticalCanvas &) = delete;
	VerticalCanvas &operator=(const VerticalCanvas &) = delete;

	// Makes the scene match the layout: adds, removes, reorders and positions items.
	// Items whose source OBS does not have are skipped and listed in missingItems().
	void sync(const VerticalLayout &layout);

	// Removes every item that refers to a source of the scene collection. Items that show
	// Program stay, because Program is not part of any collection.
	void releaseSources();

	// The frames for encoders. Creates the view on first use. Every acquire needs a release.
	video_t *acquireVideo(std::string &error);
	void releaseVideo();
	bool videoInUse() const { return videoUsers_ > 0; }

	// Keeps sources showing while the editor preview is open.
	void addPreviewUser();
	void removePreviewUser();

	// Only while no encoder uses the canvas.
	bool resize(int width, int height);

	obs_source_t *sceneSource() const;
	int width() const { return width_; }
	int height() const { return height_; }
	const std::string &layoutId() const { return layoutId_; }

	// Ids of layout items whose source was not found at the last sync.
	const std::vector<std::string> &missingItems() const { return missing_; }

	// Size of the source behind an item, in pixels. False when the item is not bound.
	bool itemSourceSize(const std::string &itemId, int &width, int &height) const;

private:
	struct Binding {
		OBSSceneItem item;
		LayoutItemKind kind = LayoutItemKind::Source;
		std::string sourceUuid;
	};

	OBSSourceAutoRelease resolveSource(const LayoutItem &item);
	void removeBinding(const std::string &itemId);

	std::string layoutId_;
	int width_ = 0;
	int height_ = 0;
	OBSSceneAutoRelease scene_;
	OBSSourceAutoRelease programMirror_;
	obs_view_t *view_ = nullptr;
	video_t *video_ = nullptr;
	int videoUsers_ = 0;
	int previewUsers_ = 0;
	std::map<std::string, Binding> bindings_; // Layout item id to scene item
	std::vector<std::string> missing_;
};

// Owns the vertical layouts of the current scene collection and a canvas for each layout that
// is in use. Loads and saves the layouts with the scene collection.
//
// Thread ownership: OBS UI thread.
class VerticalCanvasManager : public QObject {
	Q_OBJECT

public:
	VerticalCanvasManager(int canvasWidth, int canvasHeight, QObject *parent = nullptr);
	~VerticalCanvasManager() override;

	// ---- Layouts ----------------------------------------------------------------------------
	const std::vector<VerticalLayout> &layouts() const { return layouts_; }
	// Replaces the layouts, for example from the editor, and updates the live canvases.
	void setLayouts(std::vector<VerticalLayout> layouts);
	// Updates one layout in place. Used while the editor drags an item.
	void updateLayout(const VerticalLayout &layout);

	// The layout a destination uses. An empty or unknown id means the first layout.
	const VerticalLayout &layoutFor(const std::string &layoutId);
	std::string resolveLayoutId(const std::string &layoutId);

	// ---- Canvases ---------------------------------------------------------------------------
	VerticalCanvas &canvasFor(const std::string &layoutId);
	video_t *acquireVideo(const std::string &layoutId, std::string &error);
	void releaseVideo(const std::string &layoutId);
	bool anyVideoInUse() const;

	int canvasWidth() const { return width_; }
	int canvasHeight() const { return height_; }
	// Changes the canvas size and scales every layout to match. Refused while a vertical
	// destination is live.
	bool setCanvasSize(int width, int height);

	// ---- OBS integration --------------------------------------------------------------------
	// Text to store in the scene collection, and the reverse.
	std::string saveText() const;
	void loadText(const std::string &text);

	// The scene collection is about to go away. Let go of its sources.
	void releaseSources();
	// The scene collection is ready. Bind items to its sources again.
	void rebind();
	// OBS is exiting. Releases every OBS object.
	void shutdown();

Q_SIGNALS:
	void layoutsChanged();

private:
	void ensureLayout();

	int width_;
	int height_;
	std::vector<VerticalLayout> layouts_;
	std::map<std::string, std::unique_ptr<VerticalCanvas>> canvases_;
	bool shutDown_ = false;
};

} // namespace rd
