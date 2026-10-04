// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/dock_widget.h"

#include "app/app_context.h"
#include "app/background_tasks.h"
#include "app/diagnostics_service.h"
#include "app/performance_monitor.h"
#include "build_info.h"
#include "legal/legal_documents.h"
#include "outputs/output_manager.h"
#include "outputs/vertical_canvas.h"
#include "ui/destination_card.h"
#include "ui/destination_dialog.h"
#include "ui/legal_dialog.h"
#include "ui/preflight_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/update_dialog.h"
#include "ui/vertical_editor.h"
#include "utils/strings.h"

#include <obs-frontend-api.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyleOption>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rd {

namespace {

constexpr int kGridCardWidth = 300;

QString percentText(double value, int decimals)
{
	return value < 0.0 ? QStringLiteral("-") : QStringLiteral("%1%").arg(value, 0, 'f', decimals);
}

const EffectiveDestination *findEffective(const std::vector<EffectiveDestination> &all, const std::string &id)
{
	const auto found =
		std::find_if(all.begin(), all.end(), [&](const EffectiveDestination &destination) { return destination.id == id; });
	return found == all.end() ? nullptr : &*found;
}

} // namespace

// ---- CardList --------------------------------------------------------------------------------------

CardList::CardList(QWidget *parent) : QWidget(parent)
{
	grid_ = new QGridLayout(this);
	grid_->setContentsMargins(0, 0, 0, 0);
	setAcceptDrops(true);
}

int CardList::columnsForWidth(int width) const
{
	return gridMode_ ? std::max(1, width / kGridCardWidth) : 1;
}

void CardList::setCards(const std::vector<DestinationCard *> &cards)
{
	cards_ = cards;
	relayout();
}

void CardList::setGrid(bool grid)
{
	if (gridMode_ == grid)
		return;
	gridMode_ = grid;
	relayout();
}

void CardList::relayout()
{
	while (QLayoutItem *item = grid_->takeAt(0))
		delete item; // The layout item only. Cards stay alive.

	columns_ = columnsForWidth(width());
	for (size_t i = 0; i < cards_.size(); ++i) {
		const int index = static_cast<int>(i);
		grid_->addWidget(cards_[i], index / columns_, index % columns_, Qt::AlignTop);
	}
	for (int column = 0; column < columns_; ++column)
		grid_->setColumnStretch(column, 1);
	for (int column = columns_; column < 16; ++column)
		grid_->setColumnStretch(column, 0);
}

void CardList::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (columnsForWidth(width()) != columns_)
		relayout();
}

void CardList::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasFormat(QString::fromLatin1(DestinationCard::kDragMimeType)))
		event->acceptProposedAction();
}

void CardList::dragMoveEvent(QDragMoveEvent *event)
{
	if (event->mimeData()->hasFormat(QString::fromLatin1(DestinationCard::kDragMimeType)))
		event->acceptProposedAction();
}

void CardList::dropEvent(QDropEvent *event)
{
	const QByteArray payload = event->mimeData()->data(QString::fromLatin1(DestinationCard::kDragMimeType));
	const std::string id = payload.toStdString();
	if (id.empty() || cards_.empty())
		return;

	// The card under the drop point decides the place. Dropping on the second half of a card
	// means "after it".
	const QPoint position = event->position().toPoint();
	int insertBefore = static_cast<int>(cards_.size());
	int from = -1;
	for (size_t i = 0; i < cards_.size(); ++i) {
		if (cards_[i]->id() == id)
			from = static_cast<int>(i);
	}
	for (size_t i = 0; i < cards_.size(); ++i) {
		const QRect geometry = cards_[i]->geometry();
		const bool before = columns_ == 1 ? position.y() < geometry.center().y()
						  : (position.y() < geometry.bottom() && position.x() < geometry.center().x()) ||
							    position.y() < geometry.top();
		if (before) {
			insertBefore = static_cast<int>(i);
			break;
		}
	}
	if (from < 0)
		return;

	// Removing the card first shifts everything after it up by one.
	const int target = from < insertBefore ? insertBefore - 1 : insertBefore;
	event->acceptProposedAction();
	if (target != from && onReorder)
		onReorder(id, target);
}

// ---- RelayDockWidget -------------------------------------------------------------------------------

RelayDockWidget::RelayDockWidget(AppContext &app, QWidget *parent) : QWidget(parent), app_(app)
{
	setObjectName(QStringLiteral("rdRoot"));
	setMinimumWidth(240);
	theme_ = new Theme(app_, this);

	rootLayout_ = new QVBoxLayout(this);
	rootLayout_->setContentsMargins(6, 6, 6, 6);
	rootLayout_->setSpacing(6);

	buildToolbar();
	buildSections();
	theme_->attach(this);

	// A change handler may delete the card that triggered it, so run it from the event loop.
	connect(
		&app_, &AppContext::configChanged, this,
		[this] {
			if (closed_)
				return;
			syncCards();
			applyLayout();
			refreshToolbar();
			refreshNetwork();
			refreshPerformance();
			refreshLegalGate();
			update();
		},
		Qt::QueuedConnection);
	connect(&app_, &AppContext::shuttingDown, this, &RelayDockWidget::shutdownUi);

	connect(&app_.outputs(), &OutputManager::destinationChanged, this, [this](const QString &id) {
		if (closed_)
			return;
		const std::string wanted = ss(id);
		for (DestinationCard *card : cardList_->cards()) {
			if (card->id() == wanted)
				card->refresh(nullptr);
		}
		refreshToolbar();
		refreshNetwork();
	});
	connect(&app_.outputs(), &OutputManager::statsUpdated, this, [this] {
		if (closed_ || !isVisible())
			return;
		for (DestinationCard *card : cardList_->cards())
			card->refreshStats();
		refreshToolbar();
		refreshNetwork();
	});
	connect(&app_.performance(), &PerformanceMonitor::snapshotUpdated, this, [this] {
		if (!closed_ && isVisible())
			refreshPerformance();
	});
	connect(&app_.performance(), &PerformanceMonitor::suggestionsChanged, this, [this] {
		if (!closed_)
			refreshSuggestions();
	});
	connect(theme_, &Theme::changed, this, [this] {
		backgroundPath_.clear(); // Reload the background image with the new settings
		refreshToolbar();
		update();
	});

	syncCards();
	applyLayout();
	refreshToolbar();
	refreshSuggestions();
	refreshPerformance();
	refreshNetwork();
	refreshLegalGate();
}

RelayDockWidget::~RelayDockWidget()
{
	closed_ = true;
}

void RelayDockWidget::shutdownUi()
{
	// OBS is closing. Close every RelayDock window now, while OBS is still intact.
	closed_ = true;
	clipboard_.clearIfOurs();

	// Dialogs that are open right now, including a message box or a file chooser one of them
	// opened. Innermost first, so each nested event loop can end.
	std::vector<QDialog *> open;
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		auto *dialog = qobject_cast<QDialog *>(widget);
		if (!dialog || !dialog->isVisible())
			continue;
		for (QWidget *owner = dialog; owner; owner = owner->parentWidget()) {
			if (owner->objectName() == QLatin1String("rdDialog")) {
				open.push_back(dialog);
				break;
			}
		}
	}
	std::sort(open.begin(), open.end(), [](QDialog *a, QDialog *b) {
		auto depth = [](QWidget *widget) {
			int levels = 0;
			for (; widget; widget = widget->parentWidget())
				++levels;
			return levels;
		};
		return depth(a) > depth(b);
	});
	for (QDialog *dialog : open) {
		if (dialog != settings_.data())
			dialog->reject();
	}

	if (settings_)
		settings_->close();
	delete settings_.data();
}

void RelayDockWidget::onObsFinishedLoading()
{
	theme_->refresh();
	syncCards();
	refreshToolbar();
	refreshNetwork();
	refreshLegalGate();

	// The update check at start-up. It is on unless the user switched it off, and a build
	// without a project page has nothing to ask.
	const std::string repository = repositoryUrl();
	bool check = app_.config().general.checkUpdatesOnStart && !repository.empty() && !startupCheck_;
#ifdef RELAYDOCK_TEST_HOOKS
	// A test run starts OBS a hundred times, and GitHub answers 60 questions an hour. A test
	// build asks only when a test says so.
	check = check && std::getenv("RELAYDOCK_TEST_UPDATE_CHECK") != nullptr;
#endif
	if (check) {
		startupCheck_ = new UpdateChecker(this);
		connect(startupCheck_, &UpdateChecker::finished, this, [this](const UpdateResult &result) {
			// Quiet unless there is a newer version that the user has not chosen to skip.
			if (closed_ || !shouldAnnounceUpdate(result, app_.config().general.skippedUpdateVersion))
				return;
			offeredRelease_ = result.release;
			updateBanner_->setMessage("neutral", describeUpdate(result, buildInfo().version));
			updateBanner_->setAction(uiText("Update.Show", "How to update"));
			updateBanner_->show();
			// The first-run review comes first. Until it is done, the banner alone says it.
			if (legalComplete(app_.config().legal))
				showUpdate(offeredRelease_);
		});
		startupCheck_->start(repository, buildInfo().version);
	}
}

void RelayDockWidget::showUpdate(const ReleaseInfo &release)
{
	if (closed_)
		return;
	// Not modal. OBS stays usable, and the window waits until the user looks at it.
	auto *dialog = new UpdateDialog(*this, release, dialogParent());
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	connect(dialog, &QDialog::finished, this, [this, tag = release.tag] {
		// "Skip this version" also ends the reminder in the dock.
		if (!closed_ && versionOfTag(tag) == app_.config().general.skippedUpdateVersion)
			updateBanner_->hide();
	});
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void RelayDockWidget::onObsThemeChanged()
{
	theme_->refresh();
}

QWidget *RelayDockWidget::dialogParent()
{
	if (closed_)
		return nullptr;
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

// ---- Building ---------------------------------------------------------------------------------------

QPixmap RelayDockWidget::providerIcon(const std::string &providerId)
{
	const IProvider *provider = app_.providers().find(providerId);
	const int px = theme_->metrics().iconPx + 4;
	const qreal ratio = devicePixelRatioF();
	QPixmap pixmap(static_cast<int>(px * ratio), static_cast<int>(px * ratio));
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	if (!provider)
		return pixmap;

	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QColor color(qs(provider->info().accentColor));
	painter.setPen(Qt::NoPen);
	painter.setBrush(color.isValid() ? color : QColor(0x60, 0x60, 0x60));
	painter.drawRoundedRect(QRectF(0, 0, px, px), px * 0.22, px * 0.22);
	QFont font = this->font();
	font.setBold(true);
	font.setPixelSize(std::max(8, static_cast<int>(px * (provider->info().monogram.size() > 1 ? 0.42 : 0.55))));
	painter.setFont(font);
	painter.setPen(toQColor(readableOn(fromQColor(color.isValid() ? color : QColor(0x60, 0x60, 0x60)))));
	painter.drawText(QRectF(0, 0, px, px), Qt::AlignCenter, qs(provider->info().monogram));
	return pixmap;
}

void RelayDockWidget::buildToolbar()
{
	toolbar_ = new QWidget(this);
	auto *layout = new QVBoxLayout(toolbar_);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	auto *first = new QHBoxLayout();
	first->setSpacing(4);
	addButton_ = new QPushButton(uiText("Dock.AddPlatform", "+ Add Platform"), toolbar_);
	addButton_->setToolTip(uiText("Dock.AddPlatform.Tip", "Add a streaming destination"));
	auto *addMenu = new QMenu(addButton_);
	addButton_->setMenu(addMenu);
	connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] {
		addMenu->clear();
		for (const IProvider *provider : app_.providers().all()) {
			const std::string id = provider->info().id;
			addMenu->addAction(QIcon(providerIcon(id)), qs(provider->info().displayName), this,
					   [this, id] { addDestination(id); });
		}
	});
	first->addWidget(addButton_);
	first->addStretch(1);

	preflightButton_ = makeToolButton(*theme_, "stethoscope", uiText("Dock.Preflight.Tip", "Check everything before you go live"), toolbar_);
	connect(preflightButton_, &QToolButton::clicked, this, [this] { runPreflight(false); });
	first->addWidget(preflightButton_);

	viewButton_ = makeToolButton(*theme_, "maximize-2", uiText("Dock.View.Tip", "Show or hide card details"), toolbar_);
	viewButton_->setCheckable(true);
	connect(viewButton_, &QToolButton::clicked, this, [this](bool checked) {
		app_.config().activeLayout().cardView = checked ? CardView::Expanded : CardView::Compact;
		app_.notifyConfigChanged();
	});
	first->addWidget(viewButton_);

	gridButton_ = makeToolButton(*theme_, "layout-grid", uiText("Dock.Grid.Tip", "Show cards side by side"), toolbar_);
	gridButton_->setCheckable(true);
	connect(gridButton_, &QToolButton::clicked, this, [this](bool checked) {
		app_.config().activeLayout().listMode = checked ? ListMode::Grid : ListMode::List;
		app_.notifyConfigChanged();
	});
	first->addWidget(gridButton_);

	settingsButton_ = makeToolButton(*theme_, "settings", uiText("Dock.Settings.Tip", "RelayDock settings"), toolbar_);
	connect(settingsButton_, &QToolButton::clicked, this, [this] { openSettings({}); });
	first->addWidget(settingsButton_);
	layout->addLayout(first);

	auto *second = new QHBoxLayout();
	second->setSpacing(6);
	startAllButton_ = new QPushButton(uiText("Dock.StartAll", "Start All Enabled"), toolbar_);
	startAllButton_->setObjectName(QStringLiteral("rdPrimary"));
	startAllButton_->setToolTip(uiText("Dock.StartAll.Tip", "Start every destination whose box is ticked"));
	connect(startAllButton_, &QPushButton::clicked, this, &RelayDockWidget::startAllEnabled);
	stopAllButton_ = new QPushButton(uiText("Dock.StopAll", "Stop All"), toolbar_);
	stopAllButton_->setToolTip(uiText("Dock.StopAll.Tip", "Stop every destination"));
	connect(stopAllButton_, &QPushButton::clicked, this, &RelayDockWidget::stopAll);
	second->addWidget(startAllButton_, 1);
	second->addWidget(stopAllButton_, 1);
	layout->addLayout(second);

	summary_ = makeLabel(QString(), "rdSmall", toolbar_);
	layout->addWidget(summary_);
}

void RelayDockWidget::buildSections()
{
	scroll_ = new QScrollArea(this);
	scroll_->setObjectName(QStringLiteral("rdScroll"));
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	content_ = new QWidget(scroll_);
	contentLayout_ = new QVBoxLayout(content_);
	contentLayout_->setContentsMargins(0, 0, 0, 0);
	contentLayout_->setSpacing(10);
	scroll_->setWidget(content_);

	auto makeSection = [this](const char *id, const QString &title, QWidget *body) {
		auto *section = new QWidget(content_);
		auto *layout = new QVBoxLayout(section);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(4);
		layout->addWidget(makeLabel(title, "rdSection", section));
		layout->addWidget(body);
		sections_[id] = section;
		return section;
	};

	// ---- A newer release, found by the check at start-up ---------------------------------------
	updateBanner_ = new Banner(*theme_, content_);
	updateBanner_->hide();
	connect(updateBanner_, &Banner::actionClicked, this, [this] { showUpdate(offeredRelease_); });
	contentLayout_->addWidget(updateBanner_);

	// ---- First-run review ----------------------------------------------------------------------
	legalPanel_ = new QFrame(content_);
	legalPanel_->setObjectName(QStringLiteral("rdPanel"));
	{
		auto *layout = new QVBoxLayout(legalPanel_);
		layout->addWidget(makeLabel(uiText("Dock.Legal.Heading", "Before your first stream"), "rdHeading", legalPanel_));
		legalText_ = makeLabel(QString(), nullptr, legalPanel_);
		legalText_->setWordWrap(true);
		layout->addWidget(legalText_);
		auto *review = new QPushButton(uiText("Dock.Legal.Review", "Review now"), legalPanel_);
		review->setObjectName(QStringLiteral("rdPrimary"));
		connect(review, &QPushButton::clicked, this, [this] { showLegal({}); });
		layout->addWidget(review, 0, Qt::AlignLeft);
	}
	contentLayout_->addWidget(legalPanel_);

	// ---- Destinations --------------------------------------------------------------------------
	{
		auto *body = new QWidget(content_);
		auto *layout = new QVBoxLayout(body);
		layout->setContentsMargins(0, 0, 0, 0);
		cardList_ = new CardList(body);
		cardList_->onReorder = [this](const std::string &id, int index) {
			if (app_.moveDestination(id, index))
				app_.notifyConfigChanged();
		};
		emptyLabel_ = makeLabel(uiText("Dock.Empty", "No destinations yet. Choose + Add Platform to set up your first one."),
					"rdMuted", body);
		emptyLabel_->setWordWrap(true);
		layout->addWidget(cardList_);
		layout->addWidget(emptyLabel_);
		makeSection(section::kDestinations, uiText("Dock.Section.Destinations", "Destinations"), body);
	}

	// ---- Suggestions ---------------------------------------------------------------------------
	{
		auto *body = new QWidget(content_);
		suggestionsLayout_ = new QVBoxLayout(body);
		suggestionsLayout_->setContentsMargins(0, 0, 0, 0);
		suggestionsLayout_->setSpacing(6);
		makeSection(section::kSuggestions, uiText("Dock.Section.Suggestions", "Suggestions"), body);
	}

	// ---- Performance ---------------------------------------------------------------------------
	{
		auto *panel = new QFrame(content_);
		panel->setObjectName(QStringLiteral("rdPanel"));
		auto *layout = new QVBoxLayout(panel);

		auto *modeRow = new QHBoxLayout();
		modeRow->addWidget(makeLabel(uiText("Dock.Mode", "Mode"), "rdMuted", panel));
		modeCombo_ = new QComboBox(panel);
		modeCombo_->addItem(uiText("Mode.Potato", "Potato"), QStringLiteral("potato"));
		modeCombo_->addItem(uiText("Mode.Balanced", "Balanced"), QStringLiteral("balanced"));
		modeCombo_->addItem(uiText("Mode.Quality", "Quality"), QStringLiteral("quality"));
		modeCombo_->addItem(uiText("Mode.Custom", "Custom"), QStringLiteral("custom"));
		modeCombo_->setToolTip(uiText(
			"Dock.Mode.Tip",
			"Potato: lightest load, 720p at 30 FPS, one shared encoder. Balanced: 1080p at 60 FPS at most, shared encoder. Quality: each platform's own limit. Custom: your saved values."));
		modeCombo_->setAccessibleName(uiText("Dock.Mode.Name", "Performance mode"));
		modeRow->addWidget(modeCombo_, 1);
		layout->addLayout(modeRow);
		connect(modeCombo_, &QComboBox::activated, this, [this](int index) {
			PerformanceMode mode = app_.config().performanceMode;
			if (!performanceModeFromName(ss(modeCombo_->itemData(index).toString()), mode) ||
			    mode == app_.config().performanceMode)
				return;
			app_.config().performanceMode = mode;
			app_.notifyConfigChanged();
			// Live destinations take a new bitrate at once. Anything else waits for their next start.
			std::vector<std::string> active;
			for (const DestinationConfig &destination : app_.config().destinations) {
				if (app_.outputs().runtime(destination.id).active())
					active.push_back(destination.id);
			}
			app_.outputs().applyEffectiveChanges(active, false);
		});

		preventLag_ = new QCheckBox(uiText("Dock.PreventLag", "Prevent Game Lag"), panel);
		preventLag_->setToolTip(uiText(
			"Dock.PreventLag.Tip",
			"Reacts earlier to load and restores quality later, so your game keeps its frame rate. It cannot speed up a PC that is too slow for the game."));
		layout->addWidget(preventLag_);
		connect(preventLag_, &QCheckBox::toggled, this, [this](bool checked) {
			if (app_.config().optimizer.preventGameLag == checked)
				return;
			app_.config().optimizer.preventGameLag = checked;
			app_.notifyConfigChanged();
		});

		auto *grid = new QGridLayout();
		grid->setHorizontalSpacing(12);
		grid->setVerticalSpacing(2);
		const QString names[8] = {
			uiText("Perf.ObsCpu", "OBS processor"),    uiText("Perf.SystemCpu", "PC processor"),
			uiText("Perf.Gpu", "Graphics"),           uiText("Perf.Memory", "OBS memory"),
			uiText("Perf.RenderLag", "Rendering lag"), uiText("Perf.EncodeLag", "Encoder lag"),
			uiText("Perf.Dropped", "Dropped frames"),  uiText("Perf.Encoders", "Video encoders"),
		};
		for (int i = 0; i < 8; ++i) {
			const int row = i / 2;
			const int column = (i % 2) * 2;
			grid->addWidget(makeLabel(names[i], "rdSmall", panel), row, column);
			performanceValues_[i] = makeLabel(QStringLiteral("-"), "rdValue", panel);
			grid->addWidget(performanceValues_[i], row, column + 1, Qt::AlignRight);
		}
		grid->setColumnStretch(1, 1);
		grid->setColumnStretch(3, 1);
		layout->addLayout(grid);
		makeSection(section::kPerformance, uiText("Dock.Section.Performance", "Performance"), panel);
	}

	// ---- Network -------------------------------------------------------------------------------
	{
		auto *panel = new QFrame(content_);
		panel->setObjectName(QStringLiteral("rdPanel"));
		auto *layout = new QVBoxLayout(panel);
		networkLine_ = makeLabel(QString(), nullptr, panel);
		networkLine_->setWordWrap(true);
		networkMeter_ = new QProgressBar(panel);
		networkMeter_->setObjectName(QStringLiteral("rdMeter"));
		networkMeter_->setTextVisible(false);
		networkMeter_->setRange(0, 100);
		networkDetail_ = makeLabel(QString(), "rdSmall", panel);
		networkDetail_->setWordWrap(true);
		networkSetup_ = new QPushButton(uiText("Dock.Network.Setup", "Set upload speed"), panel);
		connect(networkSetup_, &QPushButton::clicked, this, [this] { openSettings("network"); });
		layout->addWidget(networkLine_);
		layout->addWidget(networkMeter_);
		layout->addWidget(networkDetail_);
		layout->addWidget(networkSetup_, 0, Qt::AlignLeft);
		makeSection(section::kNetwork, uiText("Dock.Section.Network", "Network"), panel);
	}

	contentLayout_->addStretch(1);
}

void RelayDockWidget::applyLayout()
{
	const DockLayout &layout = app_.config().activeLayout();

	// Toolbar above or below the cards.
	rootLayout_->removeWidget(toolbar_);
	rootLayout_->removeWidget(scroll_);
	if (layout.controlPlacement == ControlPlacement::Top) {
		rootLayout_->addWidget(toolbar_);
		rootLayout_->addWidget(scroll_, 1);
	} else {
		rootLayout_->addWidget(scroll_, 1);
		rootLayout_->addWidget(toolbar_);
	}

	// Sections in the saved order. The update banner and the first-run panel come first, the
	// stretch stays last.
	for (auto &entry : sections_)
		contentLayout_->removeWidget(entry.second);
	int position = 2;
	std::vector<std::string> placed;
	for (const std::string &id : layout.sectionOrder) {
		const auto found = sections_.find(id);
		if (found == sections_.end() || std::find(placed.begin(), placed.end(), id) != placed.end())
			continue;
		contentLayout_->insertWidget(position++, found->second);
		placed.push_back(id);
	}
	// A section the saved order does not mention still needs a place.
	for (auto &entry : sections_) {
		if (std::find(placed.begin(), placed.end(), entry.first) == placed.end())
			contentLayout_->insertWidget(position++, entry.second);
	}

	const bool accepted = legalComplete(app_.config().legal);
	sections_[section::kDestinations]->setVisible(accepted);
	sections_[section::kPerformance]->setVisible(accepted && layout.showPerformance);
	sections_[section::kNetwork]->setVisible(accepted && layout.showNetwork);
	refreshSuggestions();

	{
		const QSignalBlocker a(viewButton_);
		const QSignalBlocker b(gridButton_);
		viewButton_->setChecked(layout.cardView == CardView::Expanded);
		gridButton_->setChecked(layout.listMode == ListMode::Grid);
	}
	cardList_->setGrid(layout.listMode == ListMode::Grid);
	for (DestinationCard *card : cardList_->cards())
		card->setExpanded(layout.cardView == CardView::Expanded);
}

// ---- Cards ------------------------------------------------------------------------------------------

void RelayDockWidget::syncCards()
{
	const auto &destinations = app_.config().destinations;
	const std::vector<DestinationCard *> current = cardList_->cards();

	bool same = current.size() == destinations.size();
	for (size_t i = 0; same && i < current.size(); ++i)
		same = current[i]->id() == destinations[i].id;

	if (!same) {
		// Keep the cards that still exist, so a reorder does not flicker.
		std::map<std::string, DestinationCard *> byId;
		for (DestinationCard *card : current)
			byId[card->id()] = card;

		std::vector<DestinationCard *> next;
		for (const DestinationConfig &destination : destinations) {
			const auto existing = byId.find(destination.id);
			if (existing != byId.end()) {
				next.push_back(existing->second);
				byId.erase(existing);
			} else {
				next.push_back(new DestinationCard(*this, destination.id, cardList_));
			}
		}
		for (auto &leftover : byId) {
			leftover.second->hide();
			leftover.second->deleteLater();
		}
		cardList_->setCards(next);
	}

	emptyLabel_->setVisible(destinations.empty());
	refreshCards();
}

void RelayDockWidget::refreshCards()
{
	const std::vector<EffectiveDestination> enabled = app_.resolveEffective();
	const bool expanded = app_.config().activeLayout().cardView == CardView::Expanded;
	for (DestinationCard *card : cardList_->cards()) {
		const EffectiveDestination *planned = findEffective(enabled, card->id());
		if (planned) {
			card->refresh(planned);
		} else {
			// A disabled destination resolves by itself, the way a manual start would.
			const std::vector<EffectiveDestination> alone = app_.resolveEffective(card->id());
			card->refresh(findEffective(alone, card->id()));
		}
		card->setExpanded(expanded);
	}
}

// ---- Toolbar ----------------------------------------------------------------------------------------

void RelayDockWidget::refreshToolbar()
{
	OutputManager &outputs = app_.outputs();
	const bool accepted = legalComplete(app_.config().legal);

	int enabledIdle = 0;
	int active = 0;
	for (const DestinationConfig &destination : app_.config().destinations) {
		const DestinationRuntime runtime = outputs.runtime(destination.id);
		if (runtime.active() || runtime.phase == DestinationPhase::Stopping)
			++active;
		else if (destination.enabled)
			++enabledIdle;
	}

	addButton_->setEnabled(accepted);
	preflightButton_->setEnabled(accepted);
	startAllButton_->setEnabled(accepted && enabledIdle > 0);
	stopAllButton_->setEnabled(active > 0);

	const int live = outputs.liveCount();
	if (live > 0) {
		const int encoders = static_cast<int>(outputs.encoderPool().liveVideoEncoders());
		summary_->setText(uiTextF("Dock.Summary.Live", "{0} live, {1} in total, video encoders: {2}", live,
					  formatBitrate(outputs.totalBitrateKbps()), encoders));
	} else if (active > 0) {
		summary_->setText(uiText("Dock.Summary.Connecting", "Connecting..."));
	} else {
		summary_->setText(uiText("Dock.Summary.Idle", "No destination is live."));
	}
	summary_->setVisible(accepted);
}

void RelayDockWidget::startAllEnabled()
{
	if (!legalComplete(app_.config().legal)) {
		showLegal({});
		return;
	}
	if (app_.config().general.preflightOnStartAll) {
		runPreflight(true);
		return;
	}
	app_.outputs().startAllEnabled();
}

void RelayDockWidget::stopAll()
{
	if (!app_.outputs().anyActive()) {
		app_.outputs().stopAll(); // Also forces destinations that are still stopping
		return;
	}
	if (app_.config().general.confirmStopAll) {
		QMessageBox box(QMessageBox::Question, uiText("Dock.StopAll.Title", "Stop all streams"),
				uiText("Dock.StopAll.Question", "Stop every destination now?"), QMessageBox::Yes | QMessageBox::Cancel,
				dialogParent());
		box.setInformativeText(uiText("Dock.StopAll.Detail", "You can switch this question off under Settings, General."));
		box.setDefaultButton(QMessageBox::Cancel);
		if (box.exec() != QMessageBox::Yes)
			return;
	}
	app_.outputs().stopAll();
}

// ---- Panels -----------------------------------------------------------------------------------------

void RelayDockWidget::refreshSuggestions()
{
	while (QLayoutItem *item = suggestionsLayout_->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}

	PerformanceMonitor &performance = app_.performance();
	const bool accepted = legalComplete(app_.config().legal);
	const bool wanted = accepted && app_.config().activeLayout().showSuggestions;
	bool any = false;

	QWidget *body = suggestionsLayout_->parentWidget();
	for (const Suggestion &suggestion : performance.suggestions()) {
		any = true;
		auto *panel = new QFrame(body);
		panel->setObjectName(QStringLiteral("rdPanel"));
		auto *layout = new QVBoxLayout(panel);
		QLabel *title = makeLabel(qs(suggestion.text.title), "rdTitle", panel);
		title->setWordWrap(true);
		QLabel *reason = makeLabel(qs(suggestion.text.reason), nullptr, panel);
		reason->setWordWrap(true);
		QLabel *effect = makeLabel(qs(suggestion.text.effect), "rdSmall", panel);
		effect->setWordWrap(true);
		layout->addWidget(title);
		layout->addWidget(reason);
		layout->addWidget(effect);

		const std::string id = suggestion.change.id;
		auto *buttons = new QHBoxLayout();
		auto *apply = new QPushButton(uiText("Suggestion.Apply", "Apply"), panel);
		apply->setObjectName(QStringLiteral("rdPrimary"));
		auto *ignore = new QPushButton(uiText("Suggestion.Ignore", "Ignore"), panel);
		ignore->setToolTip(uiText("Suggestion.Ignore.Tip", "Dismiss this. RelayDock waits ten minutes before it proposes it again."));
		auto *lock = new QPushButton(uiText("Suggestion.Lock", "Lock Setting"), panel);
		lock->setToolTip(uiText("Suggestion.Lock.Tip",
					"Keep your own value for this setting. RelayDock stops proposing changes to it."));
		connect(apply, &QPushButton::clicked, this, [this, id] { app_.performance().applySuggestion(id); });
		connect(ignore, &QPushButton::clicked, this, [this, id] { app_.performance().ignoreSuggestion(id); });
		connect(lock, &QPushButton::clicked, this, [this, id] { app_.performance().lockSuggestion(id); });
		buttons->addWidget(apply);
		buttons->addWidget(ignore);
		buttons->addWidget(lock);
		buttons->addStretch(1);
		layout->addLayout(buttons);
		suggestionsLayout_->addWidget(panel);
	}

	for (const std::string &notice : performance.notices()) {
		any = true;
		auto *banner = new Banner(*theme_, body);
		banner->setMessage("warning", qs(notice));
		suggestionsLayout_->addWidget(banner);
	}

	// The last thing RelayDock changed by itself, so an automatic change never goes unnoticed.
	const auto &history = performance.history();
	if (!history.empty() && app_.outputs().anyActive()) {
		any = true;
		const AppliedChange &last = history.back();
		QLabel *label = makeLabel(last.automatic ? uiTextF("Suggestion.LastAutomatic", "Changed automatically: {0}", last.text)
							 : uiTextF("Suggestion.LastAccepted", "Applied: {0}", last.text),
					  "rdSmall", body);
		label->setWordWrap(true);
		suggestionsLayout_->addWidget(label);
	}

	sections_[section::kSuggestions]->setVisible(wanted && any);
}

void RelayDockWidget::refreshPerformance()
{
	const PerformanceSnapshot &snapshot = app_.performance().snapshot();
	performanceValues_[0]->setText(percentText(snapshot.obsCpuPercent, 0));
	performanceValues_[1]->setText(percentText(snapshot.systemCpuPercent, 0));
	performanceValues_[2]->setText(percentText(snapshot.gpuPercent, 0));
	performanceValues_[3]->setText(snapshot.memoryMb < 0.0 ? QStringLiteral("-")
							       : uiTextF("Perf.MemoryValue", "{0} MB", static_cast<int>(snapshot.memoryMb)));
	performanceValues_[4]->setText(percentText(snapshot.renderLagPercent, 1));
	performanceValues_[5]->setText(percentText(snapshot.encodeLagPercent, 1));
	performanceValues_[6]->setText(percentText(snapshot.worstDropPercent, 1));
	performanceValues_[7]->setText(QString::number(snapshot.videoEncoders));

	// Colour the values that mean trouble.
	auto tone = [](double value, double warn, double bad) {
		return value >= bad ? QStringLiteral("error") : value >= warn ? QStringLiteral("warning") : QString();
	};
	setStyleProperty(performanceValues_[4], "rdTone", tone(snapshot.renderLagPercent, 1.5, 5.0));
	setStyleProperty(performanceValues_[5], "rdTone", tone(snapshot.encodeLagPercent, 1.5, 5.0));
	setStyleProperty(performanceValues_[6], "rdTone", tone(snapshot.worstDropPercent, 1.5, 5.0));

	const QSignalBlocker a(modeCombo_);
	const QSignalBlocker b(preventLag_);
	modeCombo_->setCurrentIndex(modeCombo_->findData(QString::fromUtf8(performanceModeName(app_.config().performanceMode))));
	preventLag_->setChecked(app_.config().optimizer.preventGameLag);
}

void RelayDockWidget::refreshNetwork()
{
	const BandwidthBudget budget = currentBandwidth(app_);

	if (budget.entries.empty()) {
		networkLine_->setText(uiText("Dock.Network.None", "No destination is enabled."));
		networkMeter_->hide();
		networkDetail_->hide();
		networkSetup_->setVisible(budget.status == BandwidthStatus::Unknown);
		return;
	}

	if (budget.status == BandwidthStatus::Unknown) {
		networkLine_->setText(uiTextF("Dock.Network.Unknown", "Your destinations need {0} of upload.", formatBitrate(budget.requiredKbps)));
		networkMeter_->hide();
		networkDetail_->setText(uiText("Dock.Network.Unknown.Detail",
					       "Enter your upload speed and RelayDock checks that it fits. RelayDock runs no speed test."));
		networkDetail_->show();
		networkSetup_->show();
		return;
	}

	networkSetup_->hide();
	networkLine_->setText(uiTextF("Dock.Network.Known", "Needs {0} of your {1} upload", formatBitrate(budget.requiredKbps),
				      formatBitrate(budget.uploadKbps)));
	networkMeter_->setValue(std::clamp(static_cast<int>(std::lround(budget.usagePercent())), 0, 100));
	setStyleProperty(networkMeter_, "rdTone",
			 budget.status == BandwidthStatus::Exceeded ? QStringLiteral("error")
			 : budget.status == BandwidthStatus::Tight  ? QStringLiteral("warning")
								     : QString());
	networkMeter_->show();

	QString detail;
	if (budget.status != BandwidthStatus::Ok)
		detail = qs(bandwidthMessage(budget).detail) + QStringLiteral(" ") + qs(bandwidthMessage(budget).action);
	else if (budget.measuredKbps > 0)
		detail = uiTextF("Dock.Network.Sending", "Sending {0} right now. Safe limit {1}.", formatBitrate(budget.measuredKbps),
				 formatBitrate(budget.safeLimitKbps));
	else
		detail = uiTextF("Dock.Network.Safe", "Safe limit {0}.", formatBitrate(budget.safeLimitKbps));
	networkDetail_->setText(detail);
	networkDetail_->show();
}

void RelayDockWidget::refreshLegalGate()
{
	const std::vector<std::string> pending = pendingLegalDocuments(app_.config().legal);
	const bool accepted = pending.empty();
	legalPanel_->setVisible(!accepted);
	if (!accepted) {
		const bool first = app_.config().legal.empty();
		legalText_->setText(first ? uiTextF("Dock.Legal.First",
						    "RelayDock asks you to review {0} short documents first: what it does, what it stores on your PC, and what it cannot promise. It takes a few minutes and happens once.",
						    pending.size())
					  : uiTextN(static_cast<long long>(pending.size()), "Dock.Legal.Changed.One",
						    "One document changed since you last reviewed it. Review the new version to keep streaming.",
						    "Dock.Legal.Changed.Many",
						    "{0} documents changed since you last reviewed them. Review the new versions to keep streaming.",
						    pending.size()));
	}
	applyLayout();
	refreshToolbar();
}

// ---- Dialogs ----------------------------------------------------------------------------------------

void RelayDockWidget::editDestination(const std::string &id)
{
	if (closed_)
		return;
	// Credentials stay locked until the first-run review is done, whichever window asks.
	if (!legalComplete(app_.config().legal)) {
		showLegal({});
		return;
	}
	const DestinationConfig *config = app_.config().findDestination(id);
	if (!config)
		return;
	DestinationDialog dialog(*this, *config, false, dialogParent());
	dialog.exec();
}

void RelayDockWidget::addDestination(const std::string &providerId)
{
	if (closed_)
		return;
	if (!legalComplete(app_.config().legal)) {
		showLegal({});
		if (!legalComplete(app_.config().legal))
			return;
	}
	const DestinationConfig draft = app_.draftDestination(providerId);
	if (draft.id.empty())
		return;
	DestinationDialog dialog(*this, draft, true, dialogParent());
	dialog.exec();
}

void RelayDockWidget::openSettings(const std::string &pageId)
{
	if (closed_)
		return;
	if (!settings_) {
		settings_ = new SettingsDialog(*this, dialogParent());
		settings_->setAttribute(Qt::WA_DeleteOnClose, true);
	}
	if (!pageId.empty())
		settings_->showPage(pageId);
	settings_->show();
	settings_->raise();
	settings_->activateWindow();
}

void RelayDockWidget::openVerticalEditor(const std::string &layoutId)
{
	if (closed_)
		return;
	VerticalEditorDialog dialog(*this, layoutId, dialogParent());
	dialog.exec();
}

void RelayDockWidget::showLegal(const std::string &documentId)
{
	if (closed_)
		return;
	if (documentId.empty()) {
		LegalDialog dialog(*this, LegalDialog::Mode::Review, {}, dialogParent());
		dialog.exec();
		refreshLegalGate();
	} else {
		LegalDialog dialog(*this, LegalDialog::Mode::View, documentId, dialogParent());
		dialog.exec();
	}
}

void RelayDockWidget::runPreflight(bool startAfterwards)
{
	if (closed_)
		return;
	const PreflightReport report = runPreflightNow(app_);
	if (startAfterwards && report.status == PreflightStatus::Ready) {
		app_.outputs().startAllEnabled();
		return;
	}
	PreflightDialog dialog(*this, report, startAfterwards, dialogParent());
	if (dialog.exec() == QDialog::Accepted && startAfterwards)
		app_.outputs().startAllEnabled();
}

// ---- Painting ---------------------------------------------------------------------------------------

void RelayDockWidget::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	// Updates are skipped while the dock is hidden. Catch up now.
	if (closed_)
		return;
	syncCards();
	refreshToolbar();
	refreshPerformance();
	refreshNetwork();
}

void RelayDockWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	QStyleOption option;
	option.initFrom(this);
	style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

	const ThemeConfig &theme = app_.config().theme;
	if (theme.backgroundImage.empty())
		return;

	const QString path = qs(theme.backgroundImage);
	if (path != backgroundPath_) {
		backgroundPath_ = path;
		background_ = QPixmap();
		QImageReader reader(path);
		reader.setAutoTransform(true);
		// A photo from a camera is far larger than a dock. Decode it at a sensible size.
		const QSize size = reader.size();
		if (size.isValid() && (size.width() > 2560 || size.height() > 2560))
			reader.setScaledSize(size.scaled(2560, 2560, Qt::KeepAspectRatio));
		const QImage image = reader.read();
		if (!image.isNull())
			background_ = QPixmap::fromImage(image);
	}
	if (background_.isNull())
		return;

	painter.setOpacity(std::clamp(theme.backgroundOpacity, 0, 100) / 100.0);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	const QRect area = rect();
	switch (theme.backgroundFit) {
	case BackgroundFit::Stretch:
		painter.drawPixmap(area, background_);
		break;
	case BackgroundFit::Tile:
		painter.drawTiledPixmap(area, background_);
		break;
	case BackgroundFit::Center: {
		const QPoint topLeft(area.center().x() - background_.width() / 2, area.center().y() - background_.height() / 2);
		painter.setClipRect(area);
		painter.drawPixmap(topLeft, background_);
		break;
	}
	case BackgroundFit::Fit:
	case BackgroundFit::Fill: {
		const Qt::AspectRatioMode mode =
			theme.backgroundFit == BackgroundFit::Fit ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding;
		const QSize scaled = background_.size().scaled(area.size(), mode);
		const QRect target(area.center().x() - scaled.width() / 2, area.center().y() - scaled.height() / 2, scaled.width(),
				   scaled.height());
		painter.setClipRect(area);
		painter.drawPixmap(target, background_);
		break;
	}
	}
}

} // namespace rd
