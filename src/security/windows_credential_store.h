// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "security/credential_store.h"

namespace rd {

// Stores secrets in Windows Credential Manager as generic credentials.
//
// Why Credential Manager and not DPAPI-encrypted files:
//   - Windows encrypts the vault with keys tied to the user's sign-in (DPAPI underneath), so
//     both options give the same cryptographic protection.
//   - Nothing secret, not even ciphertext, lands in the OBS configuration folder. People zip
//     and share that folder when they ask for help. With this store there is nothing in it
//     to leak.
//   - The user can see and delete the entries in Windows itself (Control Panel > Credential
//     Manager > Windows Credentials), without RelayDock or OBS.
//
// What it does not protect against: any program running as the same Windows user can ask
// Windows for these entries. That is true of DPAPI files as well. See docs/security.md.
//
// Entries are named "<prefix>:<destination uuid>:<kind>". They are saved with
// CRED_PERSIST_LOCAL_MACHINE, which keeps them on this PC for this user and does not roam.
class WindowsCredentialStore final : public ICredentialStore {
public:
	static constexpr const char *kDefaultPrefix = "RelayDock";

	// Tests pass their own prefix so they never touch real entries.
	explicit WindowsCredentialStore(std::string prefix = kDefaultPrefix);

	std::string backendName() const override { return "Windows Credential Manager"; }
	bool persistent() const override { return true; }

	CredentialResult write(const CredentialId &id, const SecretString &secret) override;
	CredentialResult read(const CredentialId &id, SecretString &out) const override;
	CredentialResult remove(const CredentialId &id) override;
	bool exists(const CredentialId &id) const override;
	std::vector<CredentialId> list() const override;

	const std::string &prefix() const { return prefix_; }

private:
	std::string prefix_;
};

} // namespace rd
