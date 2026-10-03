// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "security/secret_string.h"

#include "security/secure_zero.h"

namespace rd {

SecretString::SecretString(std::string_view value) : value_(value) {}

SecretString::~SecretString()
{
	secureZero(value_);
}

SecretString::SecretString(SecretString &&other) noexcept
{
	// Copy then wipe the source. A plain std::string move may leave the bytes behind in the
	// source's small-string buffer.
	value_.assign(other.value_);
	secureZero(other.value_);
}

SecretString &SecretString::operator=(SecretString &&other) noexcept
{
	if (this != &other) {
		secureZero(value_);
		value_.assign(other.value_);
		secureZero(other.value_);
	}
	return *this;
}

SecretString SecretString::clone() const
{
	return SecretString(value_);
}

void SecretString::assign(std::string_view value)
{
	secureZero(value_);
	value_.assign(value);
}

void SecretString::clear()
{
	secureZero(value_);
}

bool SecretString::equals(const SecretString &other) const
{
	const std::string &a = value_;
	const std::string &b = other.value_;
	unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
	const size_t n = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < n; ++i)
		diff |= static_cast<unsigned char>(a[i] ^ b[i]);
	return diff == 0;
}

std::string secretMask()
{
	std::string mask;
	for (int i = 0; i < 16; ++i)
		mask += "\xE2\x80\xA2"; // U+2022 BULLET
	return mask;
}

} // namespace rd
