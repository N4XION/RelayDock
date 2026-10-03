// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "utils/i18n.h"

#include <atomic>

namespace rd {

namespace {
std::atomic<TranslateFn> g_translator{nullptr};
}

void setTranslator(TranslateFn translator)
{
	g_translator.store(translator);
}

std::string tr(const char *key, const char *english)
{
	const TranslateFn translator = g_translator.load();
	const char *translation = nullptr;
	if (translator && translator(key, &translation) && translation && translation[0] != '\0')
		return translation;
	return english;
}

namespace detail {

std::string formatTranslated(const char *key, const char *english, std::format_args args)
{
	const std::string pattern = tr(key, english);
	try {
		return std::vformat(pattern, args);
	} catch (const std::format_error &) {
		// A translation with broken placeholders must not take the interface down.
		// Fall back to the English text, which is checked at build time by the tests.
	}
	try {
		return std::vformat(english, args);
	} catch (const std::format_error &) {
		return english;
	}
}

} // namespace detail

} // namespace rd
