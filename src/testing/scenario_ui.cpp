// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// Interface steps of the scenario runner. They drive the real RelayDock windows inside a real
// OBS: open a dialog, press a button, type into a field, read what is on screen, save a
// picture of a window. Integration tests and the documentation screenshots use them.
//
// Compiled only with RELAYDOCK_TEST_HOOKS=ON.
#include "testing/scenario_runner.h"

#include "app/app_context.h"
#include "ui/dock_widget.h"
#include "utils/log.h"
#include "utils/paths.h"

#include <obs-frontend-api.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRadioButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTextBrowser>
#include <QTimer>

#include <algorithm>
#include <filesystem>

#include <windows.h>

namespace rd {

namespace {

using json = nlohmann::json;

std::string textOf(const json &object, const char *key, const std::string &fallback = {})
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

double numberOf(const json &object, const char *key, double fallback)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_number() ? it->get<double>() : fallback;
}

bool flagOf(const json &object, const char *key, bool fallback)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

QString plain(QString text)
{
	return text.remove(QLatin1Char('&'));
}

RelayDockWidget *findDock()
{
	for (QWidget *widget : QApplication::allWidgets()) {
		if (auto *dock = qobject_cast<RelayDockWidget *>(widget))
			return dock;
	}
	return nullptr;
}

QWidget *findTarget(const std::string &name)
{
	if (name == "dock")
		return findDock();
	if (name == "dockwindow") {
		RelayDockWidget *dock = findDock();
		for (QWidget *parent = dock ? dock->parentWidget() : nullptr; parent; parent = parent->parentWidget()) {
			if (qobject_cast<QDockWidget *>(parent))
				return parent;
		}
		return nullptr;
	}
	if (name == "main")
		return static_cast<QWidget *>(obs_frontend_get_main_window());
	if (name == "message") {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (widget->isVisible() && qobject_cast<QMessageBox *>(widget))
				return widget;
		}
		return nullptr;
	}
	if (name == "input") {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (widget->isVisible() && qobject_cast<QInputDialog *>(widget))
				return widget;
		}
		return nullptr;
	}
	// "dialog": the RelayDock dialog in front. A modal one wins over the settings window.
	if (QWidget *modal = QApplication::activeModalWidget()) {
		if (modal->objectName() == QLatin1String("rdDialog"))
			return modal;
	}
	QWidget *found = nullptr;
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		if (widget->isVisible() && widget->objectName() == QLatin1String("rdDialog") && qobject_cast<QDialog *>(widget)) {
			if (!found || widget->isModal())
				found = widget;
		}
	}
	return found;
}

// The label a form shows next to a field.
QString formLabel(QWidget *field)
{
	for (QWidget *widget = field; widget && widget->parentWidget(); widget = widget->parentWidget()) {
		QWidget *parent = widget->parentWidget();
		QList<QFormLayout *> forms = parent->findChildren<QFormLayout *>();
		if (auto *own = qobject_cast<QFormLayout *>(parent->layout()))
			forms.prepend(own);
		for (QFormLayout *form : forms) {
			if (auto *label = qobject_cast<QLabel *>(form->labelForField(widget)))
				return label->text();
			// A field that sits in a row of several controls.
			for (int row = 0; row < form->rowCount(); ++row) {
				QLayoutItem *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
				QLayoutItem *labelItem = form->itemAt(row, QFormLayout::LabelRole);
				if (!fieldItem || !fieldItem->layout() || !labelItem || fieldItem->layout()->indexOf(widget) < 0)
					continue;
				if (auto *label = qobject_cast<QLabel *>(labelItem->widget()))
					return label->text();
			}
		}
	}
	return QString();
}

bool matchesName(QWidget *widget, const QString &name)
{
	if (widget->accessibleName() == name || widget->objectName() == name || formLabel(widget) == name)
		return true;
	if (auto *edit = qobject_cast<QLineEdit *>(widget))
		return edit->placeholderText() == name;
	return false;
}

template <class T> T *findNamed(QWidget *root, const QString &name)
{
	for (T *candidate : root->findChildren<T *>()) {
		if (candidate->isVisibleTo(root) && matchesName(candidate, name))
			return candidate;
	}
	return nullptr;
}

// Widgets from top to bottom, then left to right, the way a person reads the window.
template <class T> QList<T *> inReadingOrder(QWidget *root, QList<T *> widgets)
{
	std::stable_sort(widgets.begin(), widgets.end(), [root](T *a, T *b) {
		const QPoint pa = a->mapTo(root, QPoint(0, 0));
		const QPoint pb = b->mapTo(root, QPoint(0, 0));
		return pa.y() != pb.y() ? pa.y() < pb.y() : pa.x() < pb.x();
	});
	return widgets;
}

QAbstractButton *findButton(QWidget *root, const QString &label)
{
	for (QAbstractButton *button : root->findChildren<QAbstractButton *>()) {
		if (!button->isVisibleTo(root))
			continue;
		if (plain(button->text()) == label || button->accessibleName() == label || button->toolTip() == label)
			return button;
	}
	return nullptr;
}

json describe(QWidget *root)
{
	json out;
	out["title"] = root->windowTitle().toStdString();
	out["visible"] = root->isVisible();
	out["width"] = root->width();
	out["height"] = root->height();

	json buttons = json::array();
	json checks = json::array();
	for (QAbstractButton *button : root->findChildren<QAbstractButton *>()) {
		if (!button->isVisibleTo(root))
			continue;
		const std::string label = plain(button->text()).toStdString();
		const std::string name = label.empty() ? button->accessibleName().toStdString() : label;
		json entry = {{"text", name}, {"enabled", button->isEnabled()}, {"tip", button->toolTip().toStdString()}};
		if (qobject_cast<QCheckBox *>(button) || qobject_cast<QRadioButton *>(button) || button->isCheckable()) {
			entry["checked"] = button->isChecked();
			checks.push_back(entry);
		} else {
			buttons.push_back(entry);
		}
	}
	out["buttons"] = buttons;
	out["checks"] = checks;

	json labels = json::array();
	for (QLabel *label : inReadingOrder(root, root->findChildren<QLabel *>())) {
		if (!label->isVisibleTo(root))
			continue;
		// Labels that shorten their text keep the full text as their accessible name.
		const QString shown = label->text().isEmpty() ? label->accessibleName() : label->text();
		if (!shown.isEmpty())
			labels.push_back({{"text", shown.toStdString()}, {"role", label->objectName().toStdString()}, {"tone", label->property("rdTone").toString().toStdString()}});
	}
	out["labels"] = labels;

	json edits = json::array();
	for (QLineEdit *edit : root->findChildren<QLineEdit *>()) {
		if (!edit->isVisibleTo(root) || qobject_cast<QAbstractSpinBox *>(edit->parentWidget()))
			continue;
		const bool hidden = edit->echoMode() != QLineEdit::Normal;
		QString name = edit->accessibleName();
		if (name.isEmpty())
			name = formLabel(edit);
		if (name.isEmpty())
			name = edit->placeholderText();
		// What a hidden field holds is never reported, only what it shows.
		edits.push_back({{"name", name.toStdString()},
				 {"hidden", hidden},
				 {"text", hidden ? std::string() : edit->text().toStdString()},
				 {"shown", edit->displayText().toStdString()},
				 {"length", edit->text().size()}});
	}
	out["edits"] = edits;

	json combos = json::array();
	for (QComboBox *combo : root->findChildren<QComboBox *>()) {
		if (!combo->isVisibleTo(root))
			continue;
		QString name = combo->accessibleName();
		if (name.isEmpty())
			name = formLabel(combo);
		json options = json::array();
		for (int i = 0; i < combo->count(); ++i)
			options.push_back(combo->itemText(i).toStdString());
		combos.push_back({{"name", name.toStdString()}, {"current", combo->currentText().toStdString()}, {"options", options}});
	}
	out["combos"] = combos;

	json spins = json::array();
	for (QAbstractSpinBox *spin : inReadingOrder(root, root->findChildren<QAbstractSpinBox *>())) {
		if (!spin->isVisibleTo(root))
			continue;
		QString name = spin->accessibleName();
		if (name.isEmpty())
			name = formLabel(spin);
		json entry = {{"name", name.toStdString()}, {"text", spin->text().toStdString()}, {"enabled", spin->isEnabled()}};
		if (auto *whole = qobject_cast<QSpinBox *>(spin))
			entry["value"] = whole->value();
		else if (auto *real = qobject_cast<QDoubleSpinBox *>(spin))
			entry["value"] = real->value();
		spins.push_back(entry);
	}
	out["spins"] = spins;

	json lists = json::array();
	for (QListWidget *list : root->findChildren<QListWidget *>()) {
		if (!list->isVisibleTo(root))
			continue;
		json rows = json::array();
		for (int i = 0; i < list->count(); ++i)
			rows.push_back(list->item(i)->text().toStdString());
		lists.push_back({{"name", list->accessibleName().toStdString()}, {"rows", rows}, {"current", list->currentRow()}});
	}
	out["lists"] = lists;
	return out;
}

} // namespace

bool ScenarioRunner::uiStep(const std::string &op, const json &step, StepResult &result, std::string &detail)
{
	if (op.rfind("ui_", 0) != 0)
		return false;
	result = StepResult::Done;

	// ---- Steps that need no target window ----------------------------------------------------
	if (op == "ui_open") {
		RelayDockWidget *dock = findDock();
		if (!dock) {
			detail = "The RelayDock dock does not exist.";
			result = StepResult::Failed;
			return true;
		}
		const std::string what = textOf(step, "what");
		std::string id;
		if (what == "edit") {
			const auto ref = refs_.find(textOf(step, "ref"));
			if (ref == refs_.end()) {
				detail = "Unknown destination ref.";
				result = StepResult::Failed;
				return true;
			}
			id = ref->second;
		}
		const std::string argument = what == "edit"       ? id
					     : what == "add"      ? textOf(step, "provider")
					     : what == "settings" ? textOf(step, "page")
					     : what == "legal"    ? textOf(step, "document")
					     : what == "vertical" ? textOf(step, "layout")
								  : std::string();
		// From the event loop. A modal dialog blocks whoever opens it, and that must not be
		// the scenario timer.
		QTimer::singleShot(0, dock, [dock, what, argument] {
			if (what == "edit")
				dock->editDestination(argument);
			else if (what == "add")
				dock->addDestination(argument);
			else if (what == "settings")
				dock->openSettings(argument);
			else if (what == "legal")
				dock->showLegal(argument);
			else if (what == "vertical")
				dock->openVerticalEditor(argument);
			else if (what == "preflight")
				dock->runPreflight(false);
			else if (what == "start_all")
				dock->runPreflight(true);
		});
		return true;
	}

	if (op == "ui_bind_ref") {
		// Gives a destination that was created through the interface a name for later steps.
		const std::string name = textOf(step, "name");
		for (const DestinationConfig &destination : app_.config().destinations) {
			if (destination.name == name) {
				refs_[textOf(step, "ref")] = destination.id;
				detail = destination.id;
				return true;
			}
		}
		detail = "No destination is named '" + name + "'.";
		result = StepResult::Failed;
		return true;
	}

	if (op == "ui_clipboard") {
		// Reports whether the clipboard holds the expected text, never the text itself.
		const QString expected = QString::fromStdString(textOf(step, "expect"));
		const QString current = QApplication::clipboard()->text();
		results_["clipboard"][textOf(step, "label", "clipboard-" + std::to_string(index_))] = {
			{"matches", !expected.isEmpty() && current == expected}, {"empty", current.isEmpty()}};
		return true;
	}

	if (op == "ui_show_dock") {
		QWidget *window = findTarget("dockwindow");
		auto *dockWindow = qobject_cast<QDockWidget *>(window);
		auto *main = qobject_cast<QMainWindow *>(findTarget("main"));
		if (!dockWindow || !main) {
			detail = "The dock window does not exist.";
			result = StepResult::Failed;
			return true;
		}
		const std::string area = textOf(step, "area", "floating");
		if (area == "floating") {
			dockWindow->setFloating(true);
			dockWindow->resize(static_cast<int>(numberOf(step, "width", 380)), static_cast<int>(numberOf(step, "height", 640)));
		} else {
			dockWindow->setFloating(false);
			main->addDockWidget(area == "left" ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea, dockWindow);
			main->resizeDocks({dockWindow}, {static_cast<int>(numberOf(step, "width", 380))}, Qt::Horizontal);
		}
		dockWindow->setVisible(true);
		dockWindow->raise();
		return true;
	}

	if (op == "ui_main_window") {
		// Size and place the OBS window for a picture.
		QWidget *main = findTarget("main");
		if (!main) {
			result = StepResult::Failed;
			return true;
		}
		main->showNormal();
		main->resize(static_cast<int>(numberOf(step, "width", 1280)), static_cast<int>(numberOf(step, "height", 760)));
		main->move(static_cast<int>(numberOf(step, "x", 40)), static_cast<int>(numberOf(step, "y", 40)));
		return true;
	}

	// ---- Steps on a window ----------------------------------------------------------------------
	const std::string targetName = textOf(step, "target", "dialog");
	QWidget *target = findTarget(targetName);

	if (op == "ui_wait") {
		const bool wantPresent = flagOf(step, "present", true);
		const bool present = target != nullptr && target->isVisible();
		if (present == wantPresent)
			return true;
		const double elapsedSec = (app_.clock().nowMs() - stepStartedMs_) / 1000.0;
		if (elapsedSec >= numberOf(step, "timeout_sec", 10.0)) {
			detail = wantPresent ? "The window '" + targetName + "' did not appear." : "The window '" + targetName + "' did not close.";
			result = StepResult::Failed;
		} else {
			result = StepResult::Waiting;
		}
		return true;
	}

	if (!target) {
		detail = "There is no window '" + targetName + "'.";
		result = StepResult::Failed;
		return true;
	}

	if (op == "ui_state") {
		results_["ui"][textOf(step, "label", "ui-" + std::to_string(index_))] = describe(target);
		return true;
	}

	if (op == "ui_grab") {
		const std::filesystem::path directory = pathFromUtf8(resultPath_).parent_path();
		const std::string file = textOf(step, "file", "grab-" + std::to_string(index_) + ".png");
		const QString path = QString::fromStdString(pathToUtf8(directory / pathFromUtf8(file)));
		const QPixmap picture = target->grab();
		if (picture.isNull() || !picture.save(path, "PNG")) {
			detail = "Could not save " + file;
			result = StepResult::Failed;
		} else {
			detail = file + " " + std::to_string(picture.width()) + "x" + std::to_string(picture.height());
		}
		return true;
	}

	if (op == "ui_capture") {
		// A picture of the window as Windows composes it, including the parts OBS draws with
		// the graphics chip, which a widget grab cannot see.
		QWidget *window = target->window();
		const HWND handle = reinterpret_cast<HWND>(window->winId());
		RECT rect{};
		GetWindowRect(handle, &rect);
		const int width = rect.right - rect.left;
		const int height = rect.bottom - rect.top;
		const std::string file = textOf(step, "file", "capture-" + std::to_string(index_) + ".png");
		bool saved = false;
		if (width > 0 && height > 0) {
			const HDC screen = GetDC(nullptr);
			const HDC memory = CreateCompatibleDC(screen);
			const HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
			const HGDIOBJ previous = SelectObject(memory, bitmap);
			const UINT renderFullContent = 0x00000002; // PW_RENDERFULLCONTENT
			if (PrintWindow(handle, memory, renderFullContent)) {
				QImage image(width, height, QImage::Format_RGB32);
				BITMAPINFO info{};
				info.bmiHeader.biSize = sizeof(info.bmiHeader);
				info.bmiHeader.biWidth = width;
				info.bmiHeader.biHeight = -height; // Top row first
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				info.bmiHeader.biCompression = BI_RGB;
				if (GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), image.bits(), &info, DIB_RGB_COLORS) > 0) {
					const std::filesystem::path directory = pathFromUtf8(resultPath_).parent_path();
					saved = image.save(QString::fromStdString(pathToUtf8(directory / pathFromUtf8(file))), "PNG");
				}
			}
			SelectObject(memory, previous);
			DeleteObject(bitmap);
			DeleteDC(memory);
			ReleaseDC(nullptr, screen);
		}
		if (!saved) {
			detail = "Windows did not provide a picture of the window.";
			result = flagOf(step, "optional", false) ? StepResult::Done : StepResult::Failed;
		} else {
			detail = file + " " + std::to_string(width) + "x" + std::to_string(height);
		}
		return true;
	}

	if (op == "ui_resize") {
		target->resize(static_cast<int>(numberOf(step, "width", target->width())),
			       static_cast<int>(numberOf(step, "height", target->height())));
		return true;
	}

	if (op == "ui_click") {
		const QString label = QString::fromStdString(textOf(step, "text"));
		QAbstractButton *button = findButton(target, label);
		if (!button) {
			detail = "No button '" + label.toStdString() + "' in '" + targetName + "'.";
			result = StepResult::Failed;
			return true;
		}
		if (!button->isEnabled()) {
			detail = "The button '" + label.toStdString() + "' is disabled.";
			result = flagOf(step, "allow_disabled", false) ? StepResult::Done : StepResult::Failed;
			return true;
		}
		// Queued, because a click can open a modal dialog and block until it closes.
		QMetaObject::invokeMethod(button, "click", Qt::QueuedConnection);
		return true;
	}

	if (op == "ui_menu") {
		// Chooses an entry from the menu of a button, such as the three dots on a card.
		const QString label = QString::fromStdString(textOf(step, "button"));
		const QString wanted = QString::fromStdString(textOf(step, "action"));
		const int index = static_cast<int>(numberOf(step, "index", 0));
		int seen = 0;
		// "index" counts matching buttons in reading order: 0 is the one on the first card.
		for (QAbstractButton *button : inReadingOrder(target, target->findChildren<QAbstractButton *>())) {
			if (!button->isVisibleTo(target))
				continue;
			if (plain(button->text()) != label && button->accessibleName() != label && button->toolTip() != label)
				continue;
			if (seen++ != index)
				continue;
			QMenu *menu = nullptr;
			if (auto *tool = qobject_cast<QToolButton *>(button))
				menu = tool->menu();
			else if (auto *push = qobject_cast<QPushButton *>(button))
				menu = push->menu();
			if (!menu) {
				detail = "The button has no menu.";
				result = StepResult::Failed;
				return true;
			}
			Q_EMIT menu->aboutToShow(); // Menus fill themselves when they open
			json entries = json::array();
			for (QAction *action : menu->actions()) {
				if (action->isSeparator())
					continue;
				entries.push_back({{"text", plain(action->text()).toStdString()}, {"enabled", action->isEnabled()}});
				if (plain(action->text()) != wanted)
					continue;
				if (!action->isEnabled()) {
					detail = "The menu entry '" + wanted.toStdString() + "' is disabled.";
					result = flagOf(step, "allow_disabled", false) ? StepResult::Done : StepResult::Failed;
					return true;
				}
				QMetaObject::invokeMethod(action, "trigger", Qt::QueuedConnection);
				return true;
			}
			// No entry wanted: just record what the menu offers.
			if (wanted.isEmpty()) {
				results_["ui"][textOf(step, "label", "menu-" + std::to_string(index_))] = entries;
				return true;
			}
			detail = "The menu has no entry '" + wanted.toStdString() + "'.";
			result = StepResult::Failed;
			return true;
		}
		detail = "No button '" + label.toStdString() + "' number " + std::to_string(index) + ".";
		result = StepResult::Failed;
		return true;
	}

	if (op == "ui_check") {
		const QString label = QString::fromStdString(textOf(step, "text"));
		const bool wanted = flagOf(step, "checked", true);
		for (QAbstractButton *button : target->findChildren<QAbstractButton *>()) {
			if (!button->isVisibleTo(target) || !button->isCheckable() || !plain(button->text()).startsWith(label))
				continue;
			if (!button->isEnabled()) {
				detail = "disabled";
				result = flagOf(step, "allow_disabled", false) ? StepResult::Done : StepResult::Failed;
				return true;
			}
			if (button->isChecked() != wanted)
				QMetaObject::invokeMethod(button, "click", Qt::QueuedConnection);
			return true;
		}
		detail = "No checkable control starts with '" + label.toStdString() + "'.";
		result = StepResult::Failed;
		return true;
	}

	if (op == "ui_text") {
		const QString name = QString::fromStdString(textOf(step, "name"));
		QLineEdit *edit = findNamed<QLineEdit>(target, name);
		if (!edit) {
			detail = "No text field '" + name.toStdString() + "'.";
			result = StepResult::Failed;
			return true;
		}
		edit->setText(QString::fromStdString(textOf(step, "value")));
		return true;
	}

	if (op == "ui_combo") {
		const QString name = QString::fromStdString(textOf(step, "name"));
		const QString value = QString::fromStdString(textOf(step, "value"));
		QComboBox *combo = findNamed<QComboBox>(target, name);
		if (!combo) {
			detail = "No list '" + name.toStdString() + "'.";
			result = StepResult::Failed;
			return true;
		}
		for (int i = 0; i < combo->count(); ++i) {
			if (!combo->itemText(i).contains(value))
				continue;
			combo->setCurrentIndex(i);
			// A real choice by the user raises "activated" too.
			Q_EMIT combo->activated(i);
			return true;
		}
		detail = "The list '" + name.toStdString() + "' has no entry with '" + value.toStdString() + "'.";
		result = StepResult::Failed;
		return true;
	}

	if (op == "ui_spin") {
		const QString name = QString::fromStdString(textOf(step, "name"));
		if (QSpinBox *spin = findNamed<QSpinBox>(target, name)) {
			spin->setValue(static_cast<int>(numberOf(step, "value", spin->value())));
			return true;
		}
		if (QDoubleSpinBox *spin = findNamed<QDoubleSpinBox>(target, name)) {
			spin->setValue(numberOf(step, "value", spin->value()));
			return true;
		}
		detail = "No number field '" + name.toStdString() + "'.";
		result = StepResult::Failed;
		return true;
	}

	if (op == "ui_select_row") {
		const QString name = QString::fromStdString(textOf(step, "name"));
		const QString value = QString::fromStdString(textOf(step, "value"));
		for (QListWidget *list : target->findChildren<QListWidget *>()) {
			if (!list->isVisibleTo(target) || (!name.isEmpty() && list->accessibleName() != name))
				continue;
			for (int i = 0; i < list->count(); ++i) {
				if (list->item(i)->text().contains(value)) {
					list->setCurrentRow(i);
					return true;
				}
			}
		}
		detail = "No list row with '" + value.toStdString() + "'.";
		result = StepResult::Failed;
		return true;
	}

	if (op == "ui_drag") {
		// Presses, moves and releases the left mouse button on a widget. Positions are shares
		// of the widget's width and height, so a test does not depend on the window size.
		const QString name = QString::fromStdString(textOf(step, "name"));
		QWidget *widget = findNamed<QWidget>(target, name);
		if (!widget) {
			detail = "No widget '" + name.toStdString() + "'.";
			result = StepResult::Failed;
			return true;
		}
		const QPointF from(widget->width() * numberOf(step, "from_x", 0.5), widget->height() * numberOf(step, "from_y", 0.5));
		const QPointF to(widget->width() * numberOf(step, "to_x", 0.5), widget->height() * numberOf(step, "to_y", 0.5));
		auto send = [widget](QEvent::Type type, const QPointF &position, Qt::MouseButton button, Qt::MouseButtons buttons) {
			QMouseEvent event(type, position, widget->mapToGlobal(position), button, buttons, Qt::NoModifier);
			QApplication::sendEvent(widget, &event);
		};
		send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
		const int stepsCount = 12;
		for (int i = 1; i <= stepsCount; ++i)
			send(QEvent::MouseMove, from + (to - from) * (static_cast<double>(i) / stepsCount), Qt::NoButton, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
		return true;
	}

	if (op == "ui_key") {
		// Presses a key on a widget, the way a keyboard user would after tabbing to it.
		// "name": "card" picks a destination card by its place in the dock.
		const QString name = QString::fromStdString(textOf(step, "name"));
		QWidget *widget = nullptr;
		if (name == QLatin1String("card")) {
			int seen = 0;
			const int index = static_cast<int>(numberOf(step, "index", 0));
			for (QFrame *card : inReadingOrder(target, target->findChildren<QFrame *>(QStringLiteral("rdCard")))) {
				if (card->isVisibleTo(target) && seen++ == index)
					widget = card;
			}
		} else {
			widget = findNamed<QWidget>(target, name);
		}
		if (!widget) {
			detail = "No widget '" + name.toStdString() + "'.";
			result = StepResult::Failed;
			return true;
		}

		const std::string keyText = textOf(step, "key");
		Qt::KeyboardModifiers modifiers = Qt::NoModifier;
		std::string keyName = keyText;
		if (keyName.rfind("Alt+", 0) == 0) {
			modifiers |= Qt::AltModifier;
			keyName = keyName.substr(4);
		}
		if (keyName.rfind("Shift+", 0) == 0) {
			modifiers |= Qt::ShiftModifier;
			keyName = keyName.substr(6);
		}
		int key = 0;
		if (keyName == "Up")
			key = Qt::Key_Up;
		else if (keyName == "Down")
			key = Qt::Key_Down;
		else if (keyName == "Left")
			key = Qt::Key_Left;
		else if (keyName == "Right")
			key = Qt::Key_Right;
		else if (keyName == "Return")
			key = Qt::Key_Return;
		if (key == 0) {
			detail = "Unknown key '" + keyText + "'.";
			result = StepResult::Failed;
			return true;
		}
		widget->setFocus(Qt::TabFocusReason);
		// Queued, because a key can open a modal window.
		QTimer::singleShot(0, widget, [widget, key, modifiers] {
			QKeyEvent press(QEvent::KeyPress, key, modifiers);
			QApplication::sendEvent(widget, &press);
			QKeyEvent release(QEvent::KeyRelease, key, modifiers);
			QApplication::sendEvent(widget, &release);
		});
		return true;
	}

	if (op == "ui_scroll_end") {
		QTextBrowser *browser = target->findChild<QTextBrowser *>();
		if (!browser) {
			detail = "The window has no text to scroll.";
			result = StepResult::Failed;
			return true;
		}
		browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
		detail = "scrolled to " + std::to_string(browser->verticalScrollBar()->maximum());
		return true;
	}

	if (op == "ui_close") {
		if (auto *dialog = qobject_cast<QDialog *>(target))
			QMetaObject::invokeMethod(dialog, "reject", Qt::QueuedConnection);
		else
			QMetaObject::invokeMethod(target, "close", Qt::QueuedConnection);
		return true;
	}

	detail = "Unknown interface step '" + op + "'.";
	result = StepResult::Failed;
	return true;
}

} // namespace rd
