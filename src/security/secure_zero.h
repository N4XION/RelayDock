// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include <cstddef>
#include <string>

namespace rd {

// Overwrites memory in a way the optimiser may not remove.
void secureZero(void *data, size_t size);

// Overwrites the whole capacity of the string, then clears it.
void secureZero(std::string &value);
void secureZero(std::wstring &value);

} // namespace rd
