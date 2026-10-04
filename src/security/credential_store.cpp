// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "security/credential_store.h"

#include "utils/uuid.h"

namespace rd {

const char *credentialKindName(CredentialKind kind)
{
	switch (kind) {
	case CredentialKind::StreamKey:
		return "stream-key";
	case CredentialKind::Password:
		return "password";
	case CredentialKind::ChatSignIn:
		return "chat-sign-in";
	case CredentialKind::ApiKey:
		return "api-key";
	}
	return "stream-key";
}

bool credentialKindFromName(std::string_view name, CredentialKind &out)
{
	if (name == "stream-key") {
		out = CredentialKind::StreamKey;
		return true;
	}
	if (name == "password") {
		out = CredentialKind::Password;
		return true;
	}
	if (name == "chat-sign-in") {
		out = CredentialKind::ChatSignIn;
		return true;
	}
	if (name == "api-key") {
		out = CredentialKind::ApiKey;
		return true;
	}
	return false;
}

CredentialResult validateCredentialId(const CredentialId &id)
{
	if (!isUuid(id.destinationId))
		return {CredentialStatus::InvalidArgument, "The destination id is not a valid UUID."};
	return {};
}

CredentialResult validateCredentialWrite(const CredentialId &id, const SecretString &secret)
{
	CredentialResult result = validateCredentialId(id);
	if (!result.ok())
		return result;
	if (secret.empty())
		return {CredentialStatus::InvalidArgument, "The secret is empty."};
	if (secret.size() > ICredentialStore::kMaxSecretBytes)
		return {CredentialStatus::TooLarge, "The secret is longer than 2048 bytes."};
	return {};
}

std::string credentialTargetName(std::string_view prefix, const CredentialId &id)
{
	std::string target(prefix);
	target += ":";
	target += id.destinationId;
	target += ":";
	target += credentialKindName(id.kind);
	return target;
}

bool parseCredentialTargetName(std::string_view prefix, std::string_view target, CredentialId &out)
{
	if (target.size() <= prefix.size() + 1 || target.substr(0, prefix.size()) != prefix ||
	    target[prefix.size()] != ':')
		return false;

	const std::string_view rest = target.substr(prefix.size() + 1);
	const size_t sep = rest.find(':');
	if (sep == std::string_view::npos)
		return false;

	const std::string_view destinationId = rest.substr(0, sep);
	const std::string_view kindName = rest.substr(sep + 1);
	CredentialKind kind;
	if (!isUuid(destinationId) || !credentialKindFromName(kindName, kind))
		return false;

	out.destinationId = std::string(destinationId);
	out.kind = kind;
	return true;
}

} // namespace rd
