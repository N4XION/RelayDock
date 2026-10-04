// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "core/vertical_layout.h"
#include "ui/ui_common.h"

#include <QDialog>
#include <QWidget>

#include <obs.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace rd {

// A live picture of one vertical canvas, drawn by OBS itself, with the selected item outlined.
// The mouse moves and resizes items. Arrow keys nudge the selected item.
//
// OBS calls the draw function on its graphics thread. It reads only the small DrawState
// below, under a mutex. Everything else belongs to the OBS UI thread.
class VerticalPreview : public QWidget {
	Q_OBJECT

public:
	explicit VerticalPreview(QWidget *parent = nullptr);
	~VerticalPreview() override;

	// What to draw. `scene` is the canvas's private scene. An empty source clears the picture.
	void setCanvas(obs_source_t *scene, int canvasWidth, int canvasHeight);
	void showLayout(const VerticalLayout &layout);
	void setSelected(const std::string &itemId);
	void setAccentColor(const QColor &color);
	// Destroys the OBS display. Called before OBS shuts its graphics down.
	void destroyDisplay();

	// The user picked an item (empty id: nothing).
	std::function<void(const std::string &itemId)> onSelect;
	// The user moved or resized an item. `finished` is true when the mouse button went up.
	std::function<void(const LayoutItem &item, bool finished)> onItemChanged;

	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	enum Handle { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8, Move = 16 };

	struct DrawState {
		OBSSource scene;
		int canvasWidth = 0;
		int canvasHeight = 0;
		bool hasSelection = false;
		RectD selection;
		float accent[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	};

	// Where the canvas sits inside the widget, in device pixels.
	struct View {
		double scale = 1.0;
		double x = 0.0;
		double y = 0.0;
	};

	void createDisplay();
	static void drawCallback(void *data, uint32_t cx, uint32_t cy);
	static View viewFor(uint32_t cx, uint32_t cy, int canvasWidth, int canvasHeight);
	bool toCanvas(const QPointF &widgetPoint, double &x, double &y) const;
	int handleAt(double x, double y) const;
	void updateCursor(int handle);
	const LayoutItem *selectedItem() const;
	void pushSelection();

	OBSDisplay display_;
	std::mutex drawMutex_;
	DrawState draw_;

	VerticalLayout layout_;
	std::string selectedId_;
	int canvasWidth_ = 0;
	int canvasHeight_ = 0;

	int dragHandle_ = None;
	double dragStartX_ = 0.0;
	double dragStartY_ = 0.0;
	LayoutItem dragOriginal_;
};

// The vertical layout editor: the preview, the list of items and their properties.
//
// Changes show in the preview at once, and on a live vertical stream too. Save keeps them and
// stores them in the OBS scene collection. Cancel puts every layout back the way it was.
class VerticalEditorDialog : public QDialog {
	Q_OBJECT

public:
	VerticalEditorDialog(UiHost &host, const std::string &layoutId, QWidget *parent = nullptr);
	~VerticalEditorDialog() override;

	void accept() override;
	void reject() override;

private:
	void attachCanvas(const std::string &layoutId);
	void detachCanvas();
	VerticalLayout *currentLayout();
	void applyLayout(bool refreshList);
	void refreshLayoutCombo();
	void refreshItems();
	void refreshProperties();
	void refreshNotes();
	void addItem(LayoutItemKind kind, const std::string &sourceName, const std::string &sourceUuid);
	LayoutItem *selectedItem();

	UiHost &host_;
	std::vector<VerticalLayout> original_; // For Cancel
	std::vector<VerticalLayout> layouts_;  // Working copy
	std::string layoutId_;
	std::string attachedId_;
	std::string selectedId_;
	bool loading_ = false;
	bool closed_ = false;

	VerticalPreview *preview_;
	QComboBox *layoutCombo_;
	QListWidget *items_;
	QPushButton *removeItem_;
	QPushButton *raiseItem_;
	QPushButton *lowerItem_;
	QSpinBox *x_;
	QSpinBox *y_;
	QSpinBox *width_;
	QSpinBox *height_;
	QComboBox *fit_;
	QSpinBox *crop_[4];
	QCheckBox *visible_;
	QCheckBox *locked_;
	QComboBox *preset_;
	QWidget *properties_;
	Banner *liveNote_;
	Banner *missingNote_;
	QLabel *sourceInfo_;
};

} // namespace rd
