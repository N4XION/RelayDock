// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "security/memory_credential_store.h"

namespace rd {

namespace {
constexpr std::string_view kPrefix = "mem";
}

MemoryCredentialStore::~MemoryCredentialStore() = default;

void MemoryCredentialStore::failWith(CredentialStatus status)
{
	std::lock_guard<std::mutex> lock(mutex_);
	forcedFailure_ = status;
}

CredentialResult MemoryCredentialStore::write(const CredentialId &id, const SecretString &secret)
{
	CredentialResult check = validateCredentialWrite(id, secret);
	if (!check.ok())
		return check;

	std::lock_guard<std::mutex> lock(mutex_);
	if (forcedFailure_ != CredentialStatus::Ok)
		return {forcedFailure_, "Forced failure."};
	entries_[credentialTargetName(kPrefix, id)] = secret.clone();
	return {};
}

CredentialResult MemoryCredentialStore::read(const CredentialId &id, SecretString &out) const
{
	CredentialResult check = validateCredentialId(id);
	if (!check.ok())
		return check;

	std::lock_guard<std::mutex> lock(mutex_);
	if (forcedFailure_ != CredentialStatus::Ok)
		return {forcedFailure_, "Forced failure."};
	const auto it = entries_.find(credentialTargetName(kPrefix, id));
	if (it == entries_.end())
		return {CredentialStatus::NotFound, "No saved secret for this destination."};
	out = it->second.clone();
	return {};
}

CredentialResult MemoryCredentialStore::remove(const CredentialId &id)
{
	CredentialResult check = validateCredentialId(id);
	if (!check.ok())
		return check;

	std::lock_guard<std::mutex> lock(mutex_);
	if (forcedFailure_ != CredentialStatus::Ok)
		return {forcedFailure_, "Forced failure."};
	const auto it = entries_.find(credentialTargetName(kPrefix, id));
	if (it == entries_.end())
		return {CredentialStatus::NotFound, "No saved secret for this destination."};
	entries_.erase(it);
	return {};
}

bool MemoryCredentialStore::exists(const CredentialId &id) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (forcedFailure_ != CredentialStatus::Ok)
		return false;
	return entries_.find(credentialTargetName(kPrefix, id)) != entries_.end();
}

std::vector<CredentialId> MemoryCredentialStore::list() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<CredentialId> ids;
	for (const auto &entry : entries_) {
		CredentialId id;
		if (parseCredentialTargetName(kPrefix, entry.first, id))
			ids.push_back(id);
	}
	return ids;
}

} // namespace rd
