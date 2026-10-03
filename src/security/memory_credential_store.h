// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "security/credential_store.h"

#include <map>
#include <mutex>

namespace rd {

// Keeps secrets in process memory only. Used by tests and as the session-only fallback when
// the operating system store cannot be used. Nothing is written to disk.
class MemoryCredentialStore final : public ICredentialStore {
public:
	~MemoryCredentialStore() override;

	std::string backendName() const override { return "Memory (this session only)"; }
	bool persistent() const override { return false; }

	CredentialResult write(const CredentialId &id, const SecretString &secret) override;
	CredentialResult read(const CredentialId &id, SecretString &out) const override;
	CredentialResult remove(const CredentialId &id) override;
	bool exists(const CredentialId &id) const override;
	std::vector<CredentialId> list() const override;

	// Test support: makes every later call fail with the given status.
	void failWith(CredentialStatus status);

private:
	mutable std::mutex mutex_;
	std::map<std::string, SecretString> entries_;
	CredentialStatus forcedFailure_ = CredentialStatus::Ok;
};

} // namespace rd
