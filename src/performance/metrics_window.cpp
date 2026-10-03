// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "performance/metrics_window.h"

namespace rd {

void MetricsWindow::add(const std::string &key, int64_t timeMs, uint64_t total, uint64_t bad)
{
	std::deque<CounterPoint> &points = counters_[key];

	// A total that went down was restarted by OBS. Start over.
	if (!points.empty() && (total < points.back().total || bad < points.back().bad))
		points.clear();

	points.push_back({timeMs, total, bad});

	// Keep one point older than the window as the baseline for the difference.
	while (points.size() > 2 && points[1].timeMs <= timeMs - windowMs_)
		points.pop_front();
}

double MetricsWindow::percent(const std::string &key) const
{
	const auto it = counters_.find(key);
	if (it == counters_.end() || it->second.size() < 2)
		return -1.0;

	const CounterPoint &first = it->second.front();
	const CounterPoint &last = it->second.back();
	if (last.timeMs - first.timeMs < windowMs_ / 2)
		return -1.0;

	const uint64_t frames = last.total - first.total;
	if (frames == 0)
		return -1.0;
	return 100.0 * static_cast<double>(last.bad - first.bad) / static_cast<double>(frames);
}

void MetricsWindow::addValue(const std::string &key, int64_t timeMs, double value)
{
	std::deque<ValuePoint> &points = values_[key];
	points.push_back({timeMs, value});
	while (!points.empty() && points.front().timeMs <= timeMs - windowMs_)
		points.pop_front();
}

double MetricsWindow::average(const std::string &key) const
{
	const auto it = values_.find(key);
	if (it == values_.end() || it->second.empty())
		return -1.0;
	double sum = 0.0;
	for (const ValuePoint &point : it->second)
		sum += point.value;
	return sum / static_cast<double>(it->second.size());
}

void MetricsWindow::remove(const std::string &key)
{
	counters_.erase(key);
	values_.erase(key);
}

void MetricsWindow::clear()
{
	counters_.clear();
	values_.clear();
}

} // namespace rd
