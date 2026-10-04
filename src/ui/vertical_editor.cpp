// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/vertical_editor.h"

#include "app/app_context.h"
#include "outputs/output_manager.h"
#include "outputs/vertical_canvas.h"
#include "utils/strings.h"
#include "utils/uuid.h"

#include <graphics/vec4.h>
#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace rd {

namespace {

constexpr double kViewMargin = 12.0;   // Device pixels around the canvas
constexpr double kHandleReach = 9.0;   // Device pixels within which an edge can be grabbed
constexpr double kSnapReach = 10.0;    // Device pixels within which an edge snaps to a guide
constexpr uint32_t kBackground = 0xFF262626;

void fillRect(gs_effect_t *solid, gs_eparam_t *colorParam, float x, float y, float width, float height, const float rgba[4])
{
	if (width <= 0.0f || height <= 0.0f)
		return;
	struct vec4 color;
	vec4_set(&color, rgba[0], rgba[1], rgba[2], rgba[3]);
	gs_effect_set_vec4(colorParam, &color);
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(width, height, 1.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, 1, 1);
	gs_matrix_pop();
}

struct SourceEntry {
	std::string name;
	std::string uuid;
	bool scene = false;
};

bool collectVideoSource(void *param, obs_source_t *source)
{
	auto *out = static_cast<std::vector<SourceEntry> *>(param);
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) == 0 || obs_source_removed(source))
		return true;
	const char *name = obs_source_get_name(source);
	const char *uuid = obs_source_get_uuid(source);
	if (name && *name)
		out->push_back({name, uuid ? uuid : "", obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE});
	return true;
}

QString presetTitle(LayoutPreset preset)
{
	switch (preset) {
	case LayoutPreset::FullCanvas:
		return uiText("VEditor.Preset.Full", "Whole canvas");
	case LayoutPreset::TopHalf:
		return uiText("VEditor.Preset.TopHalf", "Top half");
	case LayoutPreset::BottomHalf:
		return uiText("VEditor.Preset.BottomHalf", "Bottom half");
	case LayoutPreset::TopThird:
		return uiText("VEditor.Preset.TopThird", "Top third");
	case LayoutPreset::MiddleThird:
		return uiText("VEditor.Preset.MiddleThird", "Middle third");
	case LayoutPreset::BottomThird:
		return uiText("VEditor.Preset.BottomThird", "Bottom third");
	case LayoutPreset::WideTop:
		return uiText("VEditor.Preset.WideTop", "16:9 band at the top");
	case LayoutPreset::WideCenter:
		return uiText("VEditor.Preset.WideCenter", "16:9 band in the middle");
	case LayoutPreset::WideBottom:
		return uiText("VEditor.Preset.WideBottom", "16:9 band at the bottom");
	}
	return QString();
}

QString itemTitle(const LayoutItem &item)
{
	QString title = item.kind == LayoutItemKind::Program ? uiText("VEditor.Item.Program", "OBS picture (Program)") : qs(item.sourceName);
	if (!item.visible)
		title += QStringLiteral(" ") + uiText("VEditor.Item.Hidden", "(hidden)");
	if (item.locked)
		title += QStringLiteral(" ") + uiText("VEditor.Item.Locked", "(locked)");
	return title;
}

} // namespace

// ---- VerticalPreview -------------------------------------------------------------------------------

VerticalPreview::VerticalPreview(QWidget *parent) : QWidget(parent)
{
	// OBS draws into this window directly. Qt must not paint over it.
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setMinimumSize(300, 480);
	setAccessibleName(uiText("VEditor.Preview.Name", "Vertical canvas preview"));
	setAccessibleDescription(uiText("VEditor.Preview.Description",
					"Drag an item to move it. Drag an edge to resize it. Arrow keys move the selected item."));
}

VerticalPreview::~VerticalPreview()
{
	destroyDisplay();
}

void VerticalPreview::destroyDisplay()
{
	if (!display_)
		return;
	obs_display_remove_draw_callback(display_, drawCallback, this);
	display_ = nullptr; // Destroys the OBS display
}

void VerticalPreview::createDisplay()
{
	if (display_ || !isVisible() || !windowHandle())
		return;

	const qreal ratio = devicePixelRatioF();
	gs_init_data info{};
	info.cx = static_cast<uint32_t>(std::max(1.0, width() * ratio));
	info.cy = static_cast<uint32_t>(std::max(1.0, height() * ratio));
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	info.window.hwnd = reinterpret_cast<void *>(winId());

	display_ = obs_display_create(&info, kBackground);
	if (display_)
		obs_display_add_draw_callback(display_, drawCallback, this);
}

void VerticalPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	// The native window exists once the event loop has processed the show.
	QTimer::singleShot(0, this, [this] { createDisplay(); });
}

void VerticalPreview::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (display_) {
		const qreal ratio = devicePixelRatioF();
		obs_display_resize(display_, static_cast<uint32_t>(std::max(1.0, width() * ratio)),
				   static_cast<uint32_t>(std::max(1.0, height() * ratio)));
	}
}

void VerticalPreview::paintEvent(QPaintEvent *)
{
	// OBS paints this window.
}

VerticalPreview::View VerticalPreview::viewFor(uint32_t cx, uint32_t cy, int canvasWidth, int canvasHeight)
{
	View view;
	if (canvasWidth <= 0 || canvasHeight <= 0)
		return view;
	const double availableX = std::max(1.0, cx - 2.0 * kViewMargin);
	const double availableY = std::max(1.0, cy - 2.0 * kViewMargin);
	view.scale = std::min(availableX / canvasWidth, availableY / canvasHeight);
	view.x = (cx - canvasWidth * view.scale) / 2.0;
	view.y = (cy - canvasHeight * view.scale) / 2.0;
	return view;
}

// Runs on the OBS graphics thread.
void VerticalPreview::drawCallback(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<VerticalPreview *>(data);
	DrawState state;
	{
		std::lock_guard<std::mutex> lock(self->drawMutex_);
		state = self->draw_;
	}
	if (!state.scene || state.canvasWidth <= 0 || state.canvasHeight <= 0)
		return;

	const View view = viewFor(cx, cy, state.canvasWidth, state.canvasHeight);
	const float canvasW = static_cast<float>(state.canvasWidth);
	const float canvasH = static_cast<float>(state.canvasHeight);

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, canvasW, 0.0f, canvasH, -100.0f, 100.0f);
	gs_set_viewport(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(canvasW * view.scale),
			static_cast<int>(canvasH * view.scale));

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

	// The canvas is black where no item covers it, exactly as the stream is.
	const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	fillRect(solid, color, 0.0f, 0.0f, canvasW, canvasH, black);

	obs_source_video_render(state.scene);

	if (state.hasSelection) {
		// An outline two device pixels thick, whatever the zoom.
		const float t = static_cast<float>(2.0 / std::max(view.scale, 0.0001));
		const float x = static_cast<float>(state.selection.x);
		const float y = static_cast<float>(state.selection.y);
		const float w = static_cast<float>(state.selection.width);
		const float h = static_cast<float>(state.selection.height);
		fillRect(solid, color, x, y, w, t, state.accent);
		fillRect(solid, color, x, y + h - t, w, t, state.accent);
		fillRect(solid, color, x, y, t, h, state.accent);
		fillRect(solid, color, x + w - t, y, t, h, state.accent);

		// Square grips on the corners and edge centres.
		const float g = t * 3.0f;
		for (float gx : {x, x + w / 2.0f, x + w}) {
			for (float gy : {y, y + h / 2.0f, y + h}) {
				if (gx == x + w / 2.0f && gy == y + h / 2.0f)
					continue;
				fillRect(solid, color, gx - g / 2.0f, gy - g / 2.0f, g, g, state.accent);
			}
		}
	}

	gs_projection_pop();
	gs_viewport_pop();
}

void VerticalPreview::setCanvas(obs_source_t *scene, int canvasWidth, int canvasHeight)
{
	canvasWidth_ = canvasWidth;
	canvasHeight_ = canvasHeight;
	std::lock_guard<std::mutex> lock(drawMutex_);
	draw_.scene = scene;
	draw_.canvasWidth = canvasWidth;
	draw_.canvasHeight = canvasHeight;
}

void VerticalPreview::showLayout(const VerticalLayout &layout)
{
	layout_ = layout;
	pushSelection();
}

void VerticalPreview::setSelected(const std::string &itemId)
{
	selectedId_ = itemId;
	pushSelection();
}

void VerticalPreview::setAccentColor(const QColor &color)
{
	std::lock_guard<std::mutex> lock(drawMutex_);
	draw_.accent[0] = static_cast<float>(color.redF());
	draw_.accent[1] = static_cast<float>(color.greenF());
	draw_.accent[2] = static_cast<float>(color.blueF());
	draw_.accent[3] = 1.0f;
}

const LayoutItem *VerticalPreview::selectedItem() const
{
	return layout_.findItem(selectedId_);
}

void VerticalPreview::pushSelection()
{
	const LayoutItem *item = selectedItem();
	std::lock_guard<std::mutex> lock(drawMutex_);
	draw_.hasSelection = item != nullptr;
	if (item)
		draw_.selection = {item->x, item->y, item->width, item->height};
}

bool VerticalPreview::toCanvas(const QPointF &widgetPoint, double &x, double &y) const
{
	if (canvasWidth_ <= 0 || canvasHeight_ <= 0)
		return false;
	const qreal ratio = devicePixelRatioF();
	const View view = viewFor(static_cast<uint32_t>(width() * ratio), static_cast<uint32_t>(height() * ratio), canvasWidth_, canvasHeight_);
	x = (widgetPoint.x() * ratio - view.x) / view.scale;
	y = (widgetPoint.y() * ratio - view.y) / view.scale;
	return true;
}

int VerticalPreview::handleAt(double x, double y) const
{
	const LayoutItem *item = selectedItem();
	if (!item || item->locked || canvasWidth_ <= 0)
		return None;

	const qreal ratio = devicePixelRatioF();
	const View view = viewFor(static_cast<uint32_t>(width() * ratio), static_cast<uint32_t>(height() * ratio), canvasWidth_, canvasHeight_);
	const double reach = kHandleReach / std::max(view.scale, 0.0001);
	const double left = item->x;
	const double top = item->y;
	const double right = item->x + item->width;
	const double bottom = item->y + item->height;
	if (x < left - reach || x > right + reach || y < top - reach || y > bottom + reach)
		return None;

	int handle = None;
	if (std::abs(x - left) <= reach)
		handle |= Left;
	else if (std::abs(x - right) <= reach)
		handle |= Right;
	if (std::abs(y - top) <= reach)
		handle |= Top;
	else if (std::abs(y - bottom) <= reach)
		handle |= Bottom;
	return handle == None ? Move : handle;
}

void VerticalPreview::updateCursor(int handle)
{
	switch (handle) {
	case Left:
	case Right:
		setCursor(Qt::SizeHorCursor);
		break;
	case Top:
	case Bottom:
		setCursor(Qt::SizeVerCursor);
		break;
	case Left | Top:
	case Right | Bottom:
		setCursor(Qt::SizeFDiagCursor);
		break;
	case Right | Top:
	case Left | Bottom:
		setCursor(Qt::SizeBDiagCursor);
		break;
	case Move:
		setCursor(Qt::SizeAllCursor);
		break;
	default:
		unsetCursor();
		break;
	}
}

void VerticalPreview::mousePressEvent(QMouseEvent *event)
{
	setFocus(Qt::MouseFocusReason);
	double x = 0.0;
	double y = 0.0;
	if (event->button() != Qt::LeftButton || !toCanvas(event->position(), x, y))
		return;

	int handle = handleAt(x, y);
	if (handle == None || handle == Move) {
		// Maybe another item is under the pointer.
		const int index = hitTest(layout_, x, y);
		if (index >= 0) {
			const std::string id = layout_.items[static_cast<size_t>(index)].id;
			if (id != selectedId_) {
				setSelected(id);
				if (onSelect)
					onSelect(id);
			}
			handle = Move;
		} else if (handle == None) {
			setSelected({});
			if (onSelect)
				onSelect({});
		}
	}

	const LayoutItem *item = selectedItem();
	if (handle != None && item && !item->locked) {
		dragHandle_ = handle;
		dragStartX_ = x;
		dragStartY_ = y;
		dragOriginal_ = *item;
	}
}

void VerticalPreview::mouseMoveEvent(QMouseEvent *event)
{
	double x = 0.0;
	double y = 0.0;
	if (!toCanvas(event->position(), x, y))
		return;

	if (dragHandle_ == None) {
		updateCursor(handleAt(x, y));
		return;
	}

	const qreal ratio = devicePixelRatioF();
	const View view = viewFor(static_cast<uint32_t>(width() * ratio), static_cast<uint32_t>(height() * ratio), canvasWidth_, canvasHeight_);
	const double reach = kSnapReach / std::max(view.scale, 0.0001);
	const std::vector<double> guidesX = horizontalGuides(canvasWidth_);
	const std::vector<double> guidesY = verticalGuides(canvasHeight_);
	const double dx = x - dragStartX_;
	const double dy = y - dragStartY_;

	LayoutItem item = dragOriginal_;
	if (dragHandle_ == Move) {
		item.x = dragOriginal_.x + dx;
		item.y = dragOriginal_.y + dy;
		// Snap the nearest of left edge, centre and right edge. The same for top to bottom.
		auto snapAxis = [&](double position, double size, const std::vector<double> &guides) {
			for (double offset : {0.0, size / 2.0, size}) {
				const double snapped = snapToGuides(position + offset, guides, reach);
				if (snapped != position + offset)
					return snapped - offset;
			}
			return position;
		};
		item.x = snapAxis(item.x, item.width, guidesX);
		item.y = snapAxis(item.y, item.height, guidesY);
	} else {
		double left = dragOriginal_.x;
		double top = dragOriginal_.y;
		double right = dragOriginal_.x + dragOriginal_.width;
		double bottom = dragOriginal_.y + dragOriginal_.height;
		if (dragHandle_ & Left)
			left = std::min(snapToGuides(left + dx, guidesX, reach), right - kMinItemSize);
		if (dragHandle_ & Right)
			right = std::max(snapToGuides(right + dx, guidesX, reach), left + kMinItemSize);
		if (dragHandle_ & Top)
			top = std::min(snapToGuides(top + dy, guidesY, reach), bottom - kMinItemSize);
		if (dragHandle_ & Bottom)
			bottom = std::max(snapToGuides(bottom + dy, guidesY, reach), top + kMinItemSize);
		item.x = left;
		item.y = top;
		item.width = right - left;
		item.height = bottom - top;
	}
	// Whole pixels keep the picture sharp.
	item.x = std::round(item.x);
	item.y = std::round(item.y);
	item.width = std::round(item.width);
	item.height = std::round(item.height);
	clampItem(item, canvasWidth_, canvasHeight_);

	if (LayoutItem *target = layout_.findItem(selectedId_))
		*target = item;
	pushSelection();
	if (onItemChanged)
		onItemChanged(item, false);
}

void VerticalPreview::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || dragHandle_ == None)
		return;
	dragHandle_ = None;
	if (const LayoutItem *item = selectedItem(); item && onItemChanged)
		onItemChanged(*item, true);
}

void VerticalPreview::keyPressEvent(QKeyEvent *event)
{
	LayoutItem *item = layout_.findItem(selectedId_);
	if (!item || item->locked) {
		QWidget::keyPressEvent(event);
		return;
	}
	const double step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
	switch (event->key()) {
	case Qt::Key_Left:
		item->x -= step;
		break;
	case Qt::Key_Right:
		item->x += step;
		break;
	case Qt::Key_Up:
		item->y -= step;
		break;
	case Qt::Key_Down:
		item->y += step;
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	clampItem(*item, canvasWidth_, canvasHeight_);
	pushSelection();
	if (onItemChanged)
		onItemChanged(*item, true);
}

// ---- VerticalEditorDialog --------------------------------------------------------------------------

VerticalEditorDialog::VerticalEditorDialog(UiHost &host, const std::string &layoutId, QWidget *parent)
	: QDialog(parent), host_(host)
{
	setObjectName(QStringLiteral("rdDialog"));
	setWindowTitle(uiText("VEditor.Title", "RelayDock vertical layout"));
	resize(880, 760);
	AppContext &app = host_.app();
	original_ = app.vertical().layouts();
	layouts_ = original_;
	layoutId_ = app.vertical().resolveLayoutId(layoutId);

	auto *layout = new QHBoxLayout(this);
	preview_ = new VerticalPreview(this);
	layout->addWidget(preview_, 1);

	auto *side = new QWidget(this);
	side->setFixedWidth(380);
	auto *sideLayout = new QVBoxLayout(side);
	sideLayout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(side);

	liveNote_ = new Banner(host_.theme(), side);
	liveNote_->setMessage("warning", uiText("VEditor.Live", "A vertical destination is live. Viewers see your changes as you make them."));
	sideLayout->addWidget(liveNote_);

	// ---- Which layout ----------------------------------------------------------------------------
	sideLayout->addWidget(makeLabel(uiText("VEditor.Layout", "Layout"), "rdSection", side));
	auto *layoutRow = new QHBoxLayout();
	layoutCombo_ = new QComboBox(side);
	layoutCombo_->setAccessibleName(uiText("VEditor.Layout", "Layout"));
	QToolButton *newLayout = makeToolButton(host_.theme(), "plus", uiText("VEditor.Layout.New", "New layout"), side);
	QToolButton *renameLayout = makeToolButton(host_.theme(), "pencil", uiText("VEditor.Layout.Rename", "Rename this layout"), side);
	layoutRow->addWidget(layoutCombo_, 1);
	layoutRow->addWidget(newLayout);
	layoutRow->addWidget(renameLayout);
	sideLayout->addLayout(layoutRow);

	// ---- Items -----------------------------------------------------------------------------------
	sideLayout->addWidget(makeLabel(uiText("VEditor.Items", "Items, top one in front"), "rdSection", side));
	items_ = new QListWidget(side);
	items_->setAccessibleName(uiText("VEditor.Items.Name", "Layout items"));
	items_->setMaximumHeight(150);
	sideLayout->addWidget(items_);

	auto *itemButtons = new QHBoxLayout();
	auto *add = new QPushButton(uiText("VEditor.Add", "Add"), side);
	auto *addMenu = new QMenu(add);
	add->setMenu(addMenu);
	removeItem_ = new QPushButton(uiText("VEditor.Remove", "Remove"), side);
	raiseItem_ = new QPushButton(uiText("VEditor.Raise", "Forward"), side);
	lowerItem_ = new QPushButton(uiText("VEditor.Lower", "Back"), side);
	itemButtons->addWidget(add);
	itemButtons->addWidget(removeItem_);
	itemButtons->addWidget(raiseItem_);
	itemButtons->addWidget(lowerItem_);
	sideLayout->addLayout(itemButtons);

	// ---- Properties of the selected item ---------------------------------------------------------
	properties_ = new QWidget(side);
	auto *form = new QFormLayout(properties_);
	form->setContentsMargins(0, 0, 0, 0);
	auto makeSpin = [this](int minimum, int maximum, const QString &suffix) {
		auto *spin = new QSpinBox(properties_);
		spin->setRange(minimum, maximum);
		spin->setSuffix(suffix);
		return spin;
	};
	const QString px = QStringLiteral(" px");
	x_ = makeSpin(-8192, 8192, px);
	y_ = makeSpin(-8192, 8192, px);
	width_ = makeSpin(static_cast<int>(kMinItemSize), 8192, px);
	height_ = makeSpin(static_cast<int>(kMinItemSize), 8192, px);
	auto *positionRow = new QHBoxLayout();
	positionRow->addWidget(x_);
	positionRow->addWidget(y_);
	form->addRow(uiText("VEditor.Position", "Left, top"), positionRow);
	auto *sizeRow = new QHBoxLayout();
	sizeRow->addWidget(width_);
	sizeRow->addWidget(height_);
	form->addRow(uiText("VEditor.Size", "Width, height"), sizeRow);

	preset_ = new QComboBox(properties_);
	preset_->addItem(uiText("VEditor.Preset.Choose", "Place the box..."), -1);
	for (LayoutPreset preset : {LayoutPreset::FullCanvas, LayoutPreset::TopHalf, LayoutPreset::BottomHalf, LayoutPreset::TopThird,
				    LayoutPreset::MiddleThird, LayoutPreset::BottomThird, LayoutPreset::WideTop, LayoutPreset::WideCenter,
				    LayoutPreset::WideBottom})
		preset_->addItem(presetTitle(preset), static_cast<int>(preset));
	form->addRow(uiText("VEditor.Preset", "Preset"), preset_);

	fit_ = new QComboBox(properties_);
	fit_->addItem(uiText("VEditor.Fit.Fill", "Fill the box, crop the rest"), QStringLiteral("fill"));
	fit_->addItem(uiText("VEditor.Fit.Fit", "Fit inside the box"), QStringLiteral("fit"));
	fit_->setToolTip(uiText("VEditor.Fit.Tip", "Either way the source keeps its shape. RelayDock never stretches it."));
	form->addRow(uiText("VEditor.Fit", "Scaling"), fit_);

	auto *cropRow1 = new QHBoxLayout();
	auto *cropRow2 = new QHBoxLayout();
	const QString cropNames[4] = {uiText("VEditor.Crop.Left", "Crop left"), uiText("VEditor.Crop.Top", "Crop top"),
				      uiText("VEditor.Crop.Right", "Crop right"), uiText("VEditor.Crop.Bottom", "Crop bottom")};
	for (int i = 0; i < 4; ++i) {
		crop_[i] = makeSpin(0, 8192, px);
		crop_[i]->setAccessibleName(cropNames[i]);
		crop_[i]->setToolTip(cropNames[i]);
		(i % 2 == 0 ? cropRow1 : cropRow2)->addWidget(crop_[i]);
	}
	form->addRow(uiText("VEditor.Crop.LR", "Crop left, right"), cropRow1);
	form->addRow(uiText("VEditor.Crop.TB", "Crop top, bottom"), cropRow2);

	visible_ = new QCheckBox(uiText("VEditor.Visible", "Visible"), properties_);
	locked_ = new QCheckBox(uiText("VEditor.Locked", "Lock against the mouse"), properties_);
	form->addRow(visible_);
	form->addRow(locked_);
	sourceInfo_ = makeLabel(QString(), "rdSmall", properties_);
	sourceInfo_->setWordWrap(true);
	form->addRow(sourceInfo_);
	sideLayout->addWidget(properties_);

	missingNote_ = new Banner(host_.theme(), side);
	missingNote_->hide();
	sideLayout->addWidget(missingNote_);
	sideLayout->addStretch(1);

	auto *buttons = new QHBoxLayout();
	auto *cancel = new QPushButton(uiText("Common.Cancel", "Cancel"), side);
	auto *save = new QPushButton(uiText("Common.Save", "Save"), side);
	save->setObjectName(QStringLiteral("rdPrimary"));
	save->setDefault(true);
	buttons->addStretch(1);
	buttons->addWidget(cancel);
	buttons->addWidget(save);
	sideLayout->addLayout(buttons);

	// ---- Wiring ----------------------------------------------------------------------------------
	connect(cancel, &QPushButton::clicked, this, &VerticalEditorDialog::reject);
	connect(save, &QPushButton::clicked, this, &VerticalEditorDialog::accept);
	connect(&app, &AppContext::shuttingDown, this, [this] {
		// OBS is closing its graphics. Let go of everything that draws.
		preview_->destroyDisplay();
		detachCanvas();
		closed_ = true;
		QDialog::reject();
	});

	connect(layoutCombo_, &QComboBox::activated, this, [this](int index) {
		const std::string id = ss(layoutCombo_->itemData(index).toString());
		if (id == layoutId_)
			return;
		layoutId_ = id;
		selectedId_.clear();
		attachCanvas(layoutId_);
		refreshItems();
		applyLayout(false);
	});
	connect(newLayout, &QToolButton::clicked, this, [this] {
		AppContext &context = host_.app();
		VerticalLayout created = makeDefaultVerticalLayout(context.vertical().canvasWidth(), context.vertical().canvasHeight());
		created.id = generateUuid();
		created.name = locf("Vertical.NewName", "Layout {0}", layouts_.size() + 1);
		layouts_.push_back(created);
		detachCanvas();
		context.vertical().setLayouts(layouts_);
		layouts_ = context.vertical().layouts();
		layoutId_ = created.id;
		selectedId_.clear();
		attachCanvas(layoutId_);
		refreshLayoutCombo();
		refreshItems();
		applyLayout(false);
	});
	connect(renameLayout, &QToolButton::clicked, this, [this] {
		VerticalLayout *current = currentLayout();
		if (!current)
			return;
		bool ok = false;
		const QString name = QInputDialog::getText(this, uiText("Vertical.Rename.Title", "Rename layout"),
							   uiText("Vertical.Rename.Label", "Name"), QLineEdit::Normal, qs(current->name), &ok)
					     .trimmed();
		if (!ok || name.isEmpty())
			return;
		if (VerticalLayout *again = currentLayout()) {
			again->name = ss(name.left(64));
			applyLayout(false);
			refreshLayoutCombo();
		}
	});

	connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] {
		addMenu->clear();
		addMenu->addAction(uiText("VEditor.Item.Program", "OBS picture (Program)"), this,
				   [this] { addItem(LayoutItemKind::Program, {}, {}); });
		std::vector<SourceEntry> entries;
		obs_enum_scenes(collectVideoSource, &entries);
		obs_enum_sources(collectVideoSource, &entries);
		if (!entries.empty())
			addMenu->addSeparator();
		for (const SourceEntry &entry : entries) {
			const QString label = entry.scene ? uiTextF("VEditor.Add.Scene", "Scene: {0}", entry.name) : qs(entry.name);
			addMenu->addAction(label, this, [this, entry] { addItem(LayoutItemKind::Source, entry.name, entry.uuid); });
		}
	});
	connect(items_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (loading_)
			return;
		const QListWidgetItem *item = row >= 0 ? items_->item(row) : nullptr;
		selectedId_ = item ? ss(item->data(Qt::UserRole).toString()) : std::string();
		preview_->setSelected(selectedId_);
		refreshProperties();
	});
	connect(removeItem_, &QPushButton::clicked, this, [this] {
		VerticalLayout *current = currentLayout();
		if (!current || selectedId_.empty())
			return;
		std::erase_if(current->items, [this](const LayoutItem &item) { return item.id == selectedId_; });
		selectedId_.clear();
		applyLayout(true);
	});
	auto reorder = [this](int delta) {
		VerticalLayout *current = currentLayout();
		if (!current || selectedId_.empty())
			return;
		moveItem(*current, selectedId_, current->indexOf(selectedId_) + delta);
		applyLayout(true);
	};
	connect(raiseItem_, &QPushButton::clicked, this, [reorder] { reorder(1); });
	connect(lowerItem_, &QPushButton::clicked, this, [reorder] { reorder(-1); });

	// Property edits
	auto edited = [this] {
		if (loading_)
			return;
		LayoutItem *item = selectedItem();
		if (!item)
			return;
		item->x = x_->value();
		item->y = y_->value();
		item->width = width_->value();
		item->height = height_->value();
		fitModeFromName(ss(fit_->currentData().toString()), item->fit);
		item->cropLeft = crop_[0]->value();
		item->cropTop = crop_[1]->value();
		item->cropRight = crop_[2]->value();
		item->cropBottom = crop_[3]->value();
		const bool listChanges = item->visible != visible_->isChecked() || item->locked != locked_->isChecked();
		item->visible = visible_->isChecked();
		item->locked = locked_->isChecked();
		applyLayout(listChanges);
	};
	for (QSpinBox *spin : {x_, y_, width_, height_, crop_[0], crop_[1], crop_[2], crop_[3]})
		connect(spin, &QSpinBox::valueChanged, this, edited);
	connect(fit_, &QComboBox::activated, this, edited);
	connect(visible_, &QCheckBox::toggled, this, edited);
	connect(locked_, &QCheckBox::toggled, this, edited);
	connect(preset_, &QComboBox::activated, this, [this](int index) {
		const int value = preset_->itemData(index).toInt();
		preset_->setCurrentIndex(0);
		LayoutItem *item = selectedItem();
		if (value < 0 || !item)
			return;
		AppContext &context = host_.app();
		applyPreset(*item, static_cast<LayoutPreset>(value), context.vertical().canvasWidth(), context.vertical().canvasHeight());
		applyLayout(false);
		refreshProperties();
	});

	preview_->onSelect = [this](const std::string &id) {
		selectedId_ = id;
		refreshItems();
	};
	preview_->onItemChanged = [this](const LayoutItem &changed, bool) {
		if (LayoutItem *item = selectedItem()) {
			*item = changed;
			applyLayout(false);
			refreshProperties();
		}
	};

	auto applyAccent = [this] { preview_->setAccentColor(host_.theme().color(Theme::Tone::Warning)); };
	applyAccent();
	connect(&host_.theme(), &Theme::changed, this, applyAccent);

	host_.theme().attach(this);
	attachCanvas(layoutId_);
	refreshLayoutCombo();
	refreshItems();
	applyLayout(false);
}

VerticalEditorDialog::~VerticalEditorDialog()
{
	if (!closed_) {
		preview_->destroyDisplay();
		detachCanvas();
	}
}

void VerticalEditorDialog::attachCanvas(const std::string &layoutId)
{
	detachCanvas();
	AppContext &app = host_.app();
	if (app.isShutDown())
		return;
	VerticalCanvas &canvas = app.vertical().canvasFor(layoutId);
	canvas.addPreviewUser();
	attachedId_ = layoutId;
	preview_->setCanvas(canvas.sceneSource(), canvas.width(), canvas.height());
}

void VerticalEditorDialog::detachCanvas()
{
	if (attachedId_.empty())
		return;
	preview_->setCanvas(nullptr, 0, 0);
	AppContext &app = host_.app();
	if (!app.isShutDown())
		app.vertical().canvasFor(attachedId_).removePreviewUser();
	attachedId_.clear();
}

VerticalLayout *VerticalEditorDialog::currentLayout()
{
	for (VerticalLayout &layout : layouts_) {
		if (layout.id == layoutId_)
			return &layout;
	}
	return nullptr;
}

LayoutItem *VerticalEditorDialog::selectedItem()
{
	VerticalLayout *layout = currentLayout();
	return layout ? layout->findItem(selectedId_) : nullptr;
}

void VerticalEditorDialog::applyLayout(bool refreshList)
{
	VerticalLayout *layout = currentLayout();
	if (!layout || closed_)
		return;
	AppContext &app = host_.app();
	for (LayoutItem &item : layout->items)
		clampItem(item, app.vertical().canvasWidth(), app.vertical().canvasHeight());
	app.vertical().updateLayout(*layout);
	preview_->showLayout(*layout);
	preview_->setSelected(selectedId_);
	if (refreshList)
		refreshItems();
	refreshNotes();
}

void VerticalEditorDialog::refreshLayoutCombo()
{
	const QSignalBlocker blocker(layoutCombo_);
	layoutCombo_->clear();
	for (const VerticalLayout &layout : layouts_)
		layoutCombo_->addItem(qs(layout.name), qs(layout.id));
	layoutCombo_->setCurrentIndex(std::max(0, layoutCombo_->findData(qs(layoutId_))));
}

void VerticalEditorDialog::refreshItems()
{
	loading_ = true;
	items_->clear();
	if (const VerticalLayout *layout = currentLayout()) {
		// The list shows the front item first. The layout stores the back item first.
		for (auto it = layout->items.rbegin(); it != layout->items.rend(); ++it) {
			auto *entry = new QListWidgetItem(itemTitle(*it), items_);
			entry->setData(Qt::UserRole, qs(it->id));
			if (it->id == selectedId_)
				items_->setCurrentItem(entry);
		}
	}
	loading_ = false;
	preview_->setSelected(selectedId_);
	refreshProperties();
}

void VerticalEditorDialog::refreshProperties()
{
	const VerticalLayout *layout = currentLayout();
	const LayoutItem *item = selectedItem();
	properties_->setEnabled(item != nullptr);
	removeItem_->setEnabled(item != nullptr);
	const int index = layout && item ? layout->indexOf(item->id) : -1;
	raiseItem_->setEnabled(index >= 0 && index + 1 < static_cast<int>(layout->items.size()));
	lowerItem_->setEnabled(index > 0);
	if (!item) {
		sourceInfo_->setText(uiText("VEditor.NoSelection", "Select an item in the list or click it in the preview."));
		return;
	}

	loading_ = true;
	x_->setValue(static_cast<int>(std::lround(item->x)));
	y_->setValue(static_cast<int>(std::lround(item->y)));
	width_->setValue(static_cast<int>(std::lround(item->width)));
	height_->setValue(static_cast<int>(std::lround(item->height)));
	fit_->setCurrentIndex(std::max(0, fit_->findData(QString::fromUtf8(fitModeName(item->fit)))));
	crop_[0]->setValue(item->cropLeft);
	crop_[1]->setValue(item->cropTop);
	crop_[2]->setValue(item->cropRight);
	crop_[3]->setValue(item->cropBottom);
	visible_->setChecked(item->visible);
	locked_->setChecked(item->locked);
	loading_ = false;

	int sourceWidth = 0;
	int sourceHeight = 0;
	AppContext &app = host_.app();
	if (!attachedId_.empty() && app.vertical().canvasFor(attachedId_).itemSourceSize(item->id, sourceWidth, sourceHeight) &&
	    sourceWidth > 0 && sourceHeight > 0) {
		const Placement placement = computePlacement(*item, sourceWidth, sourceHeight);
		sourceInfo_->setText(uiTextF("VEditor.SourceInfo", "The source is {0}. It shows at {1} percent of its size.",
					     formatResolution(sourceWidth, sourceHeight), static_cast<int>(std::lround(placement.scale * 100.0))));
	} else {
		sourceInfo_->setText(uiText("VEditor.SourceUnknown", "OBS reports no size for this source yet."));
	}
}

void VerticalEditorDialog::refreshNotes()
{
	AppContext &app = host_.app();
	bool live = false;
	for (const DestinationConfig &destination : app.config().destinations) {
		if (destination.video.orientation == Orientation::Vertical && app.outputs().runtime(destination.id).active())
			live = true;
	}
	liveNote_->setVisible(live);

	if (attachedId_.empty()) {
		missingNote_->hide();
		return;
	}
	const std::vector<std::string> &missing = app.vertical().canvasFor(attachedId_).missingItems();
	if (missing.empty()) {
		missingNote_->hide();
		return;
	}
	std::vector<std::string> names;
	if (const VerticalLayout *layout = currentLayout()) {
		for (const std::string &id : missing) {
			if (const LayoutItem *item = layout->findItem(id))
				names.push_back(item->sourceName);
		}
	}
	missingNote_->setMessage("warning", uiTextF("VEditor.Missing", "OBS has no source named {0}. That part of the layout stays empty. Remove the item, or add a source with that name to OBS.",
						    join(names, ", ")));
	missingNote_->show();
}

void VerticalEditorDialog::addItem(LayoutItemKind kind, const std::string &sourceName, const std::string &sourceUuid)
{
	VerticalLayout *layout = currentLayout();
	if (!layout)
		return;
	AppContext &app = host_.app();
	LayoutItem item;
	item.id = generateUuid();
	item.kind = kind;
	item.sourceName = sourceName;
	item.sourceUuid = sourceUuid;
	item.fit = FitMode::Fit; // Show all of it. Cropping is a choice the user makes.
	applyPreset(item, kind == LayoutItemKind::Program ? LayoutPreset::FullCanvas : LayoutPreset::WideCenter,
		    app.vertical().canvasWidth(), app.vertical().canvasHeight());
	layout->items.push_back(item);
	selectedId_ = item.id;
	applyLayout(true);
}

void VerticalEditorDialog::accept()
{
	if (closed_)
		return;
	closed_ = true;
	preview_->destroyDisplay();
	detachCanvas();
	AppContext &app = host_.app();
	// One clean pass through the manager, then into the scene collection.
	app.vertical().setLayouts(layouts_);
	obs_frontend_save();
	app.notifyConfigChanged();
	QDialog::accept();
}

void VerticalEditorDialog::reject()
{
	if (closed_)
		return;
	closed_ = true;
	preview_->destroyDisplay();
	detachCanvas();
	// Put every layout back the way it was when the editor opened.
	host_.app().vertical().setLayouts(original_);
	QDialog::reject();
}

} // namespace rd
