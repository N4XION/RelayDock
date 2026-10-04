// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "security/credential_store.h"

namespace rd {

// The saved Twitch sign-in and the saved YouTube key live in the credential store, next to the
// stream keys. A destination has a random id. A chat account has a fixed one, so RelayDock
// finds it again. Do not change these: a saved sign-in would be lost.
inline constexpr const char *kTwitchChatAccountId = "00000000-0000-4000-8000-00007a1c0001";
inline constexpr const char *kYouTubeChatAccountId = "00000000-0000-4000-8000-00007a1c0002";

// What RelayDock saves for Twitch: the token that renews the sign-in. Never a password.
inline CredentialId twitchChatCredential()
{
	return {kTwitchChatAccountId, CredentialKind::ChatSignIn};
}

// What RelayDock saves for YouTube: the user's own API key.
inline CredentialId youtubeChatCredential()
{
	return {kYouTubeChatAccountId, CredentialKind::ApiKey};
}

} // namespace rd
