// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "ui/preflight_dialog.h"

#include "app/app_context.h"
#include "app/diagnostics_service.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace rd {

PreflightDialog::PreflightDialog(UiHost &host, PreflightReport report, bool offerStart, QWidget *parent)
	: QDialog(parent), host_(host), report_(std::move(report)), offerStart_(offerStart)
{
	setObjectName(QStringLiteral("rdDialog"));
	setWindowTitle(uiText("Preflight.Title", "RelayDock preflight check"));
	resize(560, 520);

	auto *layout = new QVBoxLayout(this);

	auto *header = new QHBoxLayout();
	status_ = new Pill(this);
	summary_ = makeLabel(QString(), "rdTitle", this);
	summary_->setWordWrap(true);
	header->addWidget(status_, 0, Qt::AlignTop);
	header->addWidget(summary_, 1);
	layout->addLayout(header);

	auto *scroll = new QScrollArea(this);
	scroll->setObjectName(QStringLiteral("rdScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget(scroll);
	items_ = new QVBoxLayout(content);
	items_->setContentsMargins(0, 0, 0, 0);
	items_->setSpacing(6);
	scroll->setWidget(content);
	layout->addWidget(scroll, 1);

	auto *buttons = new QHBoxLayout();
	auto *again = new QPushButton(uiText("Preflight.Again", "Check again"), this);
	auto *close = new QPushButton(offerStart_ ? uiText("Common.Cancel", "Cancel") : uiText("Common.Close", "Close"), this);
	start_ = new QPushButton(this);
	start_->setObjectName(QStringLiteral("rdPrimary"));
	start_->setVisible(offerStart_);
	buttons->addWidget(again);
	buttons->addStretch(1);
	buttons->addWidget(close);
	buttons->addWidget(start_);
	layout->addLayout(buttons);

	connect(again, &QPushButton::clicked, this, [this] {
		report_ = runPreflightNow(host_.app());
		showReport();
	});
	connect(close, &QPushButton::clicked, this, &QDialog::reject);
	connect(start_, &QPushButton::clicked, this, &QDialog::accept);

	host_.theme().attach(this);
	showReport();
}

void PreflightDialog::showReport()
{
	while (QLayoutItem *item = items_->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}

	const size_t failed = report_.count(PreflightStatus::Failed);
	const size_t warnings = report_.count(PreflightStatus::Warning);
	switch (report_.status) {
	case PreflightStatus::Ready:
		status_->setStatus("ok", uiText("Preflight.Status.Ready", "READY"));
		summary_->setText(uiText("Preflight.Summary.Ready", "Everything RelayDock can check looks fine."));
		break;
	case PreflightStatus::Warning:
		status_->setStatus("warning", uiText("Preflight.Status.Warning", "WARNING"));
		summary_->setText(uiTextN(static_cast<long long>(warnings), "Preflight.Summary.Warning.One", "You can stream. One point deserves a look first.",
					  "Preflight.Summary.Warning.Many", "You can stream. {0} points deserve a look first.", warnings));
		break;
	case PreflightStatus::Failed:
		status_->setStatus("error", uiText("Preflight.Status.Failed", "FAILED"));
		summary_->setText(uiTextN(static_cast<long long>(failed), "Preflight.Summary.Failed.One", "One problem needs fixing before Start All Enabled.",
					  "Preflight.Summary.Failed.Many", "{0} problems need fixing before Start All Enabled.", failed));
		break;
	}

	QWidget *parent = items_->parentWidget();
	for (const PreflightItem &item : report_.items) {
		auto *banner = new Banner(host_.theme(), parent);
		const char *tone = item.status == PreflightStatus::Failed    ? "error"
				   : item.status == PreflightStatus::Warning ? "warning"
									     : "ok";
		QString text = qs(item.title) + QStringLiteral(": ") + qs(item.message.what);
		if (!item.message.detail.empty())
			text += QStringLiteral("\n") + qs(item.message.detail);
		if (!item.message.action.empty())
			text += QStringLiteral("\n") + qs(item.message.action);
		banner->setMessage(tone, text);

		// A finding about one destination offers the way to fix it.
		if (!item.destinationId.empty() && item.status != PreflightStatus::Ready) {
			banner->setAction(uiText("Preflight.Edit", "Edit"));
			const std::string id = item.destinationId;
			connect(banner, &Banner::actionClicked, this, [this, id] {
				host_.editDestination(id);
				report_ = runPreflightNow(host_.app());
				showReport();
			});
		}
		items_->addWidget(banner);
	}
	items_->addStretch(1);

	if (offerStart_) {
		start_->setEnabled(report_.status != PreflightStatus::Failed);
		start_->setText(report_.status == PreflightStatus::Warning ? uiText("Preflight.StartAnyway", "Start anyway")
									   : uiText("Preflight.Start", "Start"));
		start_->setToolTip(report_.status == PreflightStatus::Failed
					   ? uiText("Preflight.Start.Blocked",
						    "Fix the failed points first. You can still start a single destination from its card.")
					   : QString());
	}
}

} // namespace rd
