// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "security/secure_zero.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace rd {

void secureZero(void *data, size_t size)
{
	if (!data || size == 0)
		return;
#ifdef _WIN32
	SecureZeroMemory(data, size);
#else
	volatile unsigned char *p = static_cast<volatile unsigned char *>(data);
	while (size--)
		*p++ = 0;
#endif
}

void secureZero(std::string &value)
{
	if (value.capacity() > 0) {
		value.resize(value.capacity());
		secureZero(value.data(), value.size());
	}
	value.clear();
}

void secureZero(std::wstring &value)
{
	if (value.capacity() > 0) {
		value.resize(value.capacity());
		secureZero(value.data(), value.size() * sizeof(wchar_t));
	}
	value.clear();
}

} // namespace rd
