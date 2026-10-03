// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "security/secret_string.h"

#include <string>
#include <string_view>
#include <vector>

namespace rd {

enum class CredentialKind { StreamKey, Password };

// "stream-key" or "password". Part of the stored entry name, so do not change the text.
const char *credentialKindName(CredentialKind kind);
bool credentialKindFromName(std::string_view name, CredentialKind &out);

// Identifies one secret: which destination it belongs to and what it is.
struct CredentialId {
	std::string destinationId; // UUID of the destination
	CredentialKind kind = CredentialKind::StreamKey;

	bool operator==(const CredentialId &other) const = default;
};

enum class CredentialStatus {
	Ok,
	NotFound,
	InvalidArgument, // Bad destination id or empty secret
	TooLarge,        // Secret exceeds kMaxSecretBytes
	AccessDenied,
	Unavailable,     // The operating system store cannot be used right now
	Failed,
};

struct CredentialResult {
	CredentialStatus status = CredentialStatus::Ok;
	std::string detail; // For logs and diagnostics. Never contains the secret.

	bool ok() const { return status == CredentialStatus::Ok; }
};

// Where secrets live. One implementation per operating system, plus an in-memory one for
// tests and for the session-only fallback.
//
// Implementations never write a secret to a log and never return it in CredentialResult.
// Thread safety: implementations are safe to call from any thread.
class ICredentialStore {
public:
	// Largest secret accepted, in bytes. Windows Credential Manager allows 2560.
	static constexpr size_t kMaxSecretBytes = 2048;

	virtual ~ICredentialStore() = default;

	// Human-readable name of the backend, for the Security settings page.
	virtual std::string backendName() const = 0;

	// False when secrets disappear with the process.
	virtual bool persistent() const = 0;

	virtual CredentialResult write(const CredentialId &id, const SecretString &secret) = 0;
	virtual CredentialResult read(const CredentialId &id, SecretString &out) const = 0;
	virtual CredentialResult remove(const CredentialId &id) = 0;
	virtual bool exists(const CredentialId &id) const = 0;

	// Every secret this store holds for RelayDock.
	virtual std::vector<CredentialId> list() const = 0;
};

// Checks the id and the secret size. Shared by every implementation.
CredentialResult validateCredentialWrite(const CredentialId &id, const SecretString &secret);
CredentialResult validateCredentialId(const CredentialId &id);

// "<prefix>:<destinationId>:<kind>", the entry name used by the operating system store.
std::string credentialTargetName(std::string_view prefix, const CredentialId &id);
bool parseCredentialTargetName(std::string_view prefix, std::string_view target, CredentialId &out);

} // namespace rd
