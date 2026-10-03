// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <format>
#include <string>

namespace rd {

// Text shown to the user goes through loc() or locf().
//
//   loc("Card.Start", "Start")
//   locf("Error.Rejected", "{0} rejected the connection.", providerName)
//
// The first argument is the key translators use. The second is the English text, which is
// also what the user sees when no translation exists. Because the English text lives next to
// the code that uses it, data/locale/en-US.ini is generated from the source by
// scripts/update-locale.ps1 and never edited by hand.
//
// Write both arguments as plain string literals on one call so the script can find them.
// Use numbered placeholders ({0}, {1}) so a translation may reorder them.
//
// The names are loc and locf, not tr, on purpose. Inside any QObject subclass a call to tr()
// binds to QObject::tr, which returns a QString and ignores RelayDock's locale files.

// Looks up `key`. Returns false when there is no translation.
using TranslateFn = bool (*)(const char *key, const char **translation);

// The plugin installs a function that asks OBS for the active locale. Tests leave it unset.
void setTranslator(TranslateFn translator);

std::string loc(const char *key, const char *english);

namespace detail {
std::string formatTranslated(const char *key, const char *english, std::format_args args);
}

template <class... Args> std::string locf(const char *key, const char *english, const Args &...args)
{
	return detail::formatTranslated(key, english, std::make_format_args(args...));
}

} // namespace rd
