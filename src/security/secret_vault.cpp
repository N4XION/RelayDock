// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "security/secret_vault.h"

#include "utils/log.h"

#include <algorithm>

namespace rd {

SecretVault::SecretVault(std::unique_ptr<ICredentialStore> store, Redactor &redactor)
	: store_(std::move(store)),
	  redactor_(redactor)
{
}

std::string SecretVault::backendName() const
{
	return store_->backendName();
}

SecretVault::SetResult SecretVault::set(const CredentialId &id, const SecretString &secret)
{
	SetResult out;

	const CredentialResult check = validateCredentialWrite(id, secret);
	if (!check.ok()) {
		out.result = check;
		return out;
	}

	// Register first. From here on the value cannot appear in a log line.
	redactor_.addSecret(secret.reveal());

	out.result = store_->write(id, secret);
	if (out.result.ok()) {
		// A stale session copy must not shadow the saved value.
		session_.remove(id);
		return out;
	}

	logWarning("Saving a {} for destination {} failed. {} RelayDock keeps it in memory for this session only.",
		   credentialKindName(id.kind), id.destinationId, out.result.detail);
	out.keptForSessionOnly = session_.write(id, secret).ok();
	return out;
}

CredentialResult SecretVault::get(const CredentialId &id, SecretString &out) const
{
	CredentialResult result = session_.read(id, out);
	if (!result.ok())
		result = store_->read(id, out);
	if (result.ok())
		redactor_.addSecret(out.reveal());
	return result;
}

bool SecretVault::has(const CredentialId &id) const
{
	return session_.exists(id) || store_->exists(id);
}

bool SecretVault::sessionOnly(const CredentialId &id) const
{
	return session_.exists(id) && !store_->exists(id);
}

CredentialResult SecretVault::remove(const CredentialId &id)
{
	const CredentialResult sessionResult = session_.remove(id);
	const CredentialResult storeResult = store_->remove(id);
	if (storeResult.ok() || sessionResult.ok())
		return {};
	return storeResult;
}

void SecretVault::removeDestination(const std::string &destinationId)
{
	remove({destinationId, CredentialKind::StreamKey});
	remove({destinationId, CredentialKind::Password});
}

size_t SecretVault::removeAll()
{
	size_t removed = 0;
	for (const CredentialId &id : list()) {
		if (remove(id).ok())
			++removed;
	}
	return removed;
}

size_t SecretVault::removeOrphans(const std::vector<std::string> &knownDestinationIds)
{
	size_t removed = 0;
	for (const CredentialId &id : list()) {
		const bool known = std::find(knownDestinationIds.begin(), knownDestinationIds.end(),
					     id.destinationId) != knownDestinationIds.end();
		if (!known && remove(id).ok())
			++removed;
	}
	return removed;
}

std::vector<CredentialId> SecretVault::list() const
{
	std::vector<CredentialId> ids = store_->list();
	for (const CredentialId &id : session_.list()) {
		if (std::find(ids.begin(), ids.end(), id) == ids.end())
			ids.push_back(id);
	}
	return ids;
}

void SecretVault::primeRedactor() const
{
	for (const CredentialId &id : list()) {
		SecretString value;
		get(id, value); // get() registers the value
	}
}

} // namespace rd
