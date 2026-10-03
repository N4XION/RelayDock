// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "outputs/vertical_canvas.h"

#include "outputs/program_mirror_source.h"
#include "utils/log.h"

#include <graphics/vec2.h>
#include <obs.h>

#include <algorithm>
#include <format>

namespace rd {

// ---- VerticalCanvas ------------------------------------------------------------------------------

VerticalCanvas::VerticalCanvas(std::string layoutId, int width, int height)
	: layoutId_(std::move(layoutId)),
	  width_(width),
	  height_(height)
{
	const std::string name = std::format("RelayDock vertical canvas {}", layoutId_);
	scene_ = obs_scene_create_private(name.c_str());
}

VerticalCanvas::~VerticalCanvas()
{
	// Encoders are gone by now: the output manager releases them before it releases video.
	while (previewUsers_ > 0)
		removePreviewUser();

	if (view_) {
		if (video_) {
			obs_source_dec_active(sceneSource());
			obs_view_remove(view_);
			video_ = nullptr;
		}
		obs_view_set_source(view_, 0, nullptr);
		obs_view_destroy(view_);
		view_ = nullptr;
	}

	for (auto &entry : bindings_)
		obs_sceneitem_remove(entry.second.item);
	bindings_.clear();
}

obs_source_t *VerticalCanvas::sceneSource() const
{
	return scene_ ? obs_scene_get_source(scene_) : nullptr;
}

OBSSourceAutoRelease VerticalCanvas::resolveSource(const LayoutItem &item)
{
	if (item.kind == LayoutItemKind::Program) {
		if (!programMirror_)
			programMirror_ = obs_source_create_private(kProgramMirrorSourceId, "RelayDock Program", nullptr);
		return OBSSourceAutoRelease(obs_source_get_ref(programMirror_));
	}

	obs_source_t *source = nullptr;
	if (!item.sourceUuid.empty())
		source = obs_get_source_by_uuid(item.sourceUuid.c_str());
	if (!source && !item.sourceName.empty())
		source = obs_get_source_by_name(item.sourceName.c_str());

	if (source && obs_source_removed(source)) {
		obs_source_release(source);
		source = nullptr;
	}
	return OBSSourceAutoRelease(source);
}

void VerticalCanvas::removeBinding(const std::string &itemId)
{
	const auto it = bindings_.find(itemId);
	if (it == bindings_.end())
		return;
	obs_sceneitem_remove(it->second.item);
	bindings_.erase(it);
}

void VerticalCanvas::sync(const VerticalLayout &layout)
{
	if (!scene_)
		return;
	missing_.clear();

	// Drop scene items whose layout item is gone or now points at another source.
	std::vector<std::string> stale;
	for (const auto &entry : bindings_) {
		const LayoutItem *item = layout.findItem(entry.first);
		bool keep = item && item->kind == entry.second.kind;
		if (keep && item->kind == LayoutItemKind::Source) {
			obs_source_t *bound = obs_sceneitem_get_source(entry.second.item);
			if (!bound || obs_source_removed(bound)) {
				keep = false;
			} else if (!item->sourceUuid.empty()) {
				keep = item->sourceUuid == entry.second.sourceUuid;
			} else {
				const char *name = obs_source_get_name(bound);
				keep = name && item->sourceName == name;
			}
		}
		if (!keep)
			stale.push_back(entry.first);
	}
	for (const std::string &id : stale)
		removeBinding(id);

	// Add what is missing and position everything.
	for (const LayoutItem &item : layout.items) {
		auto binding = bindings_.find(item.id);
		if (binding == bindings_.end()) {
			OBSSourceAutoRelease source = resolveSource(item);
			obs_sceneitem_t *sceneItem = source ? obs_scene_add(scene_, source) : nullptr;
			if (!sceneItem) {
				missing_.push_back(item.id);
				continue;
			}
			Binding created;
			created.item = sceneItem;
			created.kind = item.kind;
			const char *uuid = obs_source_get_uuid(source);
			created.sourceUuid = uuid ? uuid : "";
			binding = bindings_.emplace(item.id, std::move(created)).first;
		}

		obs_sceneitem_t *sceneItem = binding->second.item;

		// The box plus a scale rule. SCALE_INNER fits the source inside the box. SCALE_OUTER
		// covers the box, and crop_to_bounds cuts off what sticks out. OBS keeps the source's
		// proportions in both, so the picture is never stretched.
		obs_transform_info info{};
		vec2_set(&info.pos, static_cast<float>(item.x), static_cast<float>(item.y));
		info.rot = 0.0f;
		vec2_set(&info.scale, 1.0f, 1.0f);
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		info.bounds_type = item.fit == FitMode::Fill ? OBS_BOUNDS_SCALE_OUTER : OBS_BOUNDS_SCALE_INNER;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		vec2_set(&info.bounds, static_cast<float>(item.width), static_cast<float>(item.height));
		info.crop_to_bounds = item.fit == FitMode::Fill;
		obs_sceneitem_set_info2(sceneItem, &info);

		obs_sceneitem_crop crop{};
		crop.left = item.cropLeft;
		crop.top = item.cropTop;
		crop.right = item.cropRight;
		crop.bottom = item.cropBottom;
		obs_sceneitem_set_crop(sceneItem, &crop);

		obs_sceneitem_set_visible(sceneItem, item.visible);
	}

	// Stack order: the first layout item is at the bottom.
	int position = 0;
	for (const LayoutItem &item : layout.items) {
		const auto binding = bindings_.find(item.id);
		if (binding != bindings_.end())
			obs_sceneitem_set_order_position(binding->second.item, position++);
	}
}

void VerticalCanvas::releaseSources()
{
	std::vector<std::string> ids;
	for (const auto &entry : bindings_) {
		if (entry.second.kind == LayoutItemKind::Source)
			ids.push_back(entry.first);
	}
	for (const std::string &id : ids)
		removeBinding(id);
}

video_t *VerticalCanvas::acquireVideo(std::string &error)
{
	if (!scene_) {
		error = "OBS did not create the vertical scene.";
		return nullptr;
	}

	if (videoUsers_ == 0) {
		obs_video_info ovi{};
		if (!obs_get_video_info(&ovi)) {
			error = "OBS video is not running.";
			return nullptr;
		}
		// Same frame rate and colour settings as OBS, at the vertical size.
		ovi.base_width = static_cast<uint32_t>(width_);
		ovi.base_height = static_cast<uint32_t>(height_);
		ovi.output_width = static_cast<uint32_t>(width_);
		ovi.output_height = static_cast<uint32_t>(height_);

		if (!view_)
			view_ = obs_view_create();
		obs_view_set_source(view_, 0, sceneSource());
		video_ = obs_view_add2(view_, &ovi);
		if (!video_) {
			obs_view_set_source(view_, 0, nullptr);
			error = "OBS could not add the vertical canvas to its render loop.";
			return nullptr;
		}
		// A view only marks its sources as showing. Mark them active too, so sources that
		// act on activation (media playback, for example) behave as they do in Program.
		obs_source_inc_active(sceneSource());
		logInfo("Vertical canvas {}x{} joined the OBS render loop.", width_, height_);
	}

	++videoUsers_;
	return video_;
}

void VerticalCanvas::releaseVideo()
{
	if (videoUsers_ <= 0)
		return;
	if (--videoUsers_ > 0)
		return;

	obs_source_dec_active(sceneSource());
	// OBS frees the render target on its graphics thread at the next frame.
	obs_view_remove(view_);
	obs_view_set_source(view_, 0, nullptr);
	video_ = nullptr;
	logInfo("Vertical canvas left the OBS render loop.");
}

void VerticalCanvas::addPreviewUser()
{
	if (previewUsers_++ == 0 && sceneSource())
		obs_source_inc_showing(sceneSource());
}

void VerticalCanvas::removePreviewUser()
{
	if (previewUsers_ <= 0)
		return;
	if (--previewUsers_ == 0 && sceneSource())
		obs_source_dec_showing(sceneSource());
}

bool VerticalCanvas::resize(int width, int height)
{
	if (videoInUse())
		return false;
	width_ = width;
	height_ = height;
	return true;
}

bool VerticalCanvas::itemSourceSize(const std::string &itemId, int &width, int &height) const
{
	const auto it = bindings_.find(itemId);
	if (it == bindings_.end())
		return false;
	obs_source_t *source = obs_sceneitem_get_source(it->second.item);
	if (!source)
		return false;
	width = static_cast<int>(obs_source_get_width(source));
	height = static_cast<int>(obs_source_get_height(source));
	return width > 0 && height > 0;
}

// ---- VerticalCanvasManager -----------------------------------------------------------------------

VerticalCanvasManager::VerticalCanvasManager(int canvasWidth, int canvasHeight, QObject *parent)
	: QObject(parent),
	  width_(canvasWidth),
	  height_(canvasHeight)
{
}

VerticalCanvasManager::~VerticalCanvasManager()
{
	shutdown();
}

void VerticalCanvasManager::ensureLayout()
{
	if (layouts_.empty())
		layouts_.push_back(makeDefaultVerticalLayout(width_, height_));
}

const VerticalLayout &VerticalCanvasManager::layoutFor(const std::string &layoutId)
{
	ensureLayout();
	for (const VerticalLayout &layout : layouts_) {
		if (layout.id == layoutId)
			return layout;
	}
	return layouts_.front();
}

std::string VerticalCanvasManager::resolveLayoutId(const std::string &layoutId)
{
	return layoutFor(layoutId).id;
}

VerticalCanvas &VerticalCanvasManager::canvasFor(const std::string &layoutId)
{
	const VerticalLayout &layout = layoutFor(layoutId);
	auto it = canvases_.find(layout.id);
	if (it == canvases_.end()) {
		it = canvases_.emplace(layout.id, std::make_unique<VerticalCanvas>(layout.id, width_, height_)).first;
		it->second->sync(layout);
	}
	return *it->second;
}

video_t *VerticalCanvasManager::acquireVideo(const std::string &layoutId, std::string &error)
{
	if (shutDown_) {
		error = "RelayDock is shutting down.";
		return nullptr;
	}
	VerticalCanvas &canvas = canvasFor(layoutId);
	canvas.sync(layoutFor(layoutId));
	return canvas.acquireVideo(error);
}

void VerticalCanvasManager::releaseVideo(const std::string &layoutId)
{
	// The id was resolved when the stream started. Look it up as it is, because the layout
	// may have been deleted while the stream ran.
	auto it = canvases_.find(layoutId);
	if (it == canvases_.end())
		it = canvases_.find(resolveLayoutId(layoutId));
	if (it == canvases_.end())
		return;

	it->second->releaseVideo();

	const bool layoutExists = std::any_of(layouts_.begin(), layouts_.end(),
					      [&](const VerticalLayout &layout) { return layout.id == it->first; });
	if (!layoutExists && !it->second->videoInUse())
		canvases_.erase(it);
}

bool VerticalCanvasManager::anyVideoInUse() const
{
	return std::any_of(canvases_.begin(), canvases_.end(),
			   [](const auto &entry) { return entry.second->videoInUse(); });
}

void VerticalCanvasManager::setLayouts(std::vector<VerticalLayout> layouts)
{
	layouts_ = std::move(layouts);
	for (const std::string &note : sanitizeVerticalLayouts(layouts_, width_, height_))
		logWarning("Vertical layouts: {}", note);
	ensureLayout();
	rebind();
	Q_EMIT layoutsChanged();
}

void VerticalCanvasManager::updateLayout(const VerticalLayout &layout)
{
	for (VerticalLayout &existing : layouts_) {
		if (existing.id == layout.id) {
			existing = layout;
			const auto canvas = canvases_.find(layout.id);
			if (canvas != canvases_.end())
				canvas->second->sync(existing);
			return;
		}
	}
}

bool VerticalCanvasManager::setCanvasSize(int width, int height)
{
	if (width == width_ && height == height_)
		return true;
	if (anyVideoInUse())
		return false;

	const double factorX = static_cast<double>(width) / width_;
	const double factorY = static_cast<double>(height) / height_;
	for (VerticalLayout &layout : layouts_)
		scaleLayout(layout, factorX, factorY);
	width_ = width;
	height_ = height;

	for (auto &entry : canvases_)
		entry.second->resize(width_, height_);
	rebind();
	Q_EMIT layoutsChanged();
	return true;
}

std::string VerticalCanvasManager::saveText() const
{
	return serializeVerticalLayouts(layouts_, width_, height_);
}

void VerticalCanvasManager::loadText(const std::string &text)
{
	std::vector<VerticalLayout> loaded;
	int savedWidth = 0;
	int savedHeight = 0;
	if (!text.empty() && !parseVerticalLayouts(text, loaded, &savedWidth, &savedHeight)) {
		logWarning("The vertical layouts saved in this scene collection could not be read. The default layout is used.");
		loaded.clear();
	}

	// Layouts are saved in canvas pixels. Scale them when the canvas size differs now.
	if (savedWidth > 0 && savedHeight > 0 && (savedWidth != width_ || savedHeight != height_)) {
		for (VerticalLayout &layout : loaded)
			scaleLayout(layout, static_cast<double>(width_) / savedWidth, static_cast<double>(height_) / savedHeight);
	}

	layouts_ = std::move(loaded);
	for (const std::string &note : sanitizeVerticalLayouts(layouts_, width_, height_))
		logWarning("Vertical layouts: {}", note);
	ensureLayout();
	rebind();
	Q_EMIT layoutsChanged();
}

void VerticalCanvasManager::releaseSources()
{
	for (auto &entry : canvases_)
		entry.second->releaseSources();
}

void VerticalCanvasManager::rebind()
{
	for (auto it = canvases_.begin(); it != canvases_.end();) {
		const std::string &id = it->first;
		const auto layout = std::find_if(layouts_.begin(), layouts_.end(),
						 [&](const VerticalLayout &candidate) { return candidate.id == id; });
		if (layout != layouts_.end()) {
			it->second->sync(*layout);
			++it;
		} else if (it->second->videoInUse()) {
			// The layout was deleted while a stream uses it. Keep the canvas until the
			// stream stops. Only its collection sources go.
			it->second->releaseSources();
			++it;
		} else {
			it = canvases_.erase(it);
		}
	}
}

void VerticalCanvasManager::shutdown()
{
	if (shutDown_)
		return;
	shutDown_ = true;
	canvases_.clear();
}

} // namespace rd
