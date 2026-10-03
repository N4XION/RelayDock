// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "security/credential_store.h"
#include "security/memory_credential_store.h"
#include "security/redactor.h"

#include <memory>

namespace rd {

// The one place RelayDock reads and writes stream keys and passwords.
//
// It wraps the operating system store and adds two guarantees:
//   1. Every secret that passes through is registered with the redactor, so it cannot appear
//      in a log line or a diagnostic export afterwards.
//   2. A secret is never written to disk in plain text. When the operating system store
//      fails, the secret stays in memory for this session only and the caller is told.
//
// Thread safety: safe to call from any thread.
class SecretVault {
public:
	SecretVault(std::unique_ptr<ICredentialStore> store, Redactor &redactor);

	// Name of the persistent backend, for display.
	std::string backendName() const;

	struct SetResult {
		CredentialResult result;        // Outcome of the persistent write.
		bool keptForSessionOnly = false; // True when the write failed and memory holds the secret.
	};

	SetResult set(const CredentialId &id, const SecretString &secret);
	CredentialResult get(const CredentialId &id, SecretString &out) const;
	bool has(const CredentialId &id) const;

	// True when the secret exists only in memory and will be gone after OBS closes.
	bool sessionOnly(const CredentialId &id) const;

	CredentialResult remove(const CredentialId &id);

	// Removes the stream key and the password of one destination.
	void removeDestination(const std::string &destinationId);

	// Removes every secret RelayDock saved. Returns how many entries were removed.
	size_t removeAll();

	// Removes saved secrets whose destination is not in the list. Returns how many.
	size_t removeOrphans(const std::vector<std::string> &knownDestinationIds);

	std::vector<CredentialId> list() const;

	// Reads every saved secret once and registers it with the redactor. Call at startup so
	// logs are protected before the first stream starts.
	void primeRedactor() const;

private:
	std::unique_ptr<ICredentialStore> store_;
	mutable MemoryCredentialStore session_;
	Redactor &redactor_;
};

} // namespace rd
