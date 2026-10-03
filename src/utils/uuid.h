// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <string>
#include <string_view>

namespace rd {

// Random (version 4) UUID in lower-case 8-4-4-4-12 form.
std::string generateUuid();

// True for the canonical 8-4-4-4-12 hex form, upper or lower case.
bool isUuid(std::string_view text);

} // namespace rd
