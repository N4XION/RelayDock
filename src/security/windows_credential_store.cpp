// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "security/windows_credential_store.h"

#include "security/secure_zero.h"

#include <windows.h>
#include <wincred.h>

#include <format>

namespace rd {

namespace {

std::wstring toWide(std::string_view utf8)
{
	if (utf8.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring wide(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
	return wide;
}

std::string toUtf8(std::wstring_view wide)
{
	if (wide.empty())
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
					       nullptr, nullptr);
	std::string utf8(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length, nullptr,
			    nullptr);
	return utf8;
}

CredentialResult fromWindowsError(DWORD error, const char *operation)
{
	CredentialStatus status = CredentialStatus::Failed;
	switch (error) {
	case ERROR_NOT_FOUND:
		status = CredentialStatus::NotFound;
		break;
	case ERROR_ACCESS_DENIED:
		status = CredentialStatus::AccessDenied;
		break;
	case ERROR_NO_SUCH_LOGON_SESSION:
	case ERROR_SERVICE_DISABLED:
	case ERROR_SERVICE_NOT_ACTIVE:
		status = CredentialStatus::Unavailable;
		break;
	case ERROR_INVALID_PARAMETER:
	case ERROR_BAD_USERNAME:
		status = CredentialStatus::InvalidArgument;
		break;
	default:
		break;
	}
	return {status, std::format("Windows Credential Manager {} failed with error {}.", operation, error)};
}

} // namespace

WindowsCredentialStore::WindowsCredentialStore(std::string prefix) : prefix_(std::move(prefix)) {}

CredentialResult WindowsCredentialStore::write(const CredentialId &id, const SecretString &secret)
{
	CredentialResult check = validateCredentialWrite(id, secret);
	if (!check.ok())
		return check;

	std::wstring target = toWide(credentialTargetName(prefix_, id));
	std::wstring userName = L"RelayDock";
	std::wstring comment = id.kind == CredentialKind::StreamKey
				       ? L"Stream key saved by the RelayDock plugin for OBS Studio"
				       : L"RTMP password saved by the RelayDock plugin for OBS Studio";

	CREDENTIALW credential{};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = target.data();
	credential.Comment = comment.data();
	credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.reveal().data()));
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	credential.UserName = userName.data();

	if (!CredWriteW(&credential, 0))
		return fromWindowsError(GetLastError(), "write");
	return {};
}

CredentialResult WindowsCredentialStore::read(const CredentialId &id, SecretString &out) const
{
	CredentialResult check = validateCredentialId(id);
	if (!check.ok())
		return check;

	const std::wstring target = toWide(credentialTargetName(prefix_, id));
	PCREDENTIALW credential = nullptr;
	if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
		return fromWindowsError(GetLastError(), "read");

	out.assign(std::string_view(reinterpret_cast<const char *>(credential->CredentialBlob),
				    credential->CredentialBlobSize));

	// Wipe our copy of the blob before Windows frees it.
	secureZero(credential->CredentialBlob, credential->CredentialBlobSize);
	CredFree(credential);

	if (out.empty())
		return {CredentialStatus::NotFound, "The saved entry is empty."};
	return {};
}

CredentialResult WindowsCredentialStore::remove(const CredentialId &id)
{
	CredentialResult check = validateCredentialId(id);
	if (!check.ok())
		return check;

	const std::wstring target = toWide(credentialTargetName(prefix_, id));
	if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
		return fromWindowsError(GetLastError(), "delete");
	return {};
}

bool WindowsCredentialStore::exists(const CredentialId &id) const
{
	if (!validateCredentialId(id).ok())
		return false;

	const std::wstring target = toWide(credentialTargetName(prefix_, id));
	PCREDENTIALW credential = nullptr;
	if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
		return false;

	const bool hasValue = credential->CredentialBlobSize > 0;
	secureZero(credential->CredentialBlob, credential->CredentialBlobSize);
	CredFree(credential);
	return hasValue;
}

std::vector<CredentialId> WindowsCredentialStore::list() const
{
	std::vector<CredentialId> ids;

	const std::wstring filter = toWide(prefix_ + ":*");
	DWORD count = 0;
	PCREDENTIALW *credentials = nullptr;
	if (!CredEnumerateW(filter.c_str(), 0, &count, &credentials))
		return ids; // ERROR_NOT_FOUND means there are none.

	for (DWORD i = 0; i < count; ++i) {
		PCREDENTIALW credential = credentials[i];
		if (credential->Type == CRED_TYPE_GENERIC && credential->TargetName) {
			CredentialId id;
			if (parseCredentialTargetName(prefix_, toUtf8(credential->TargetName), id))
				ids.push_back(std::move(id));
		}
		// CredEnumerate returns the blobs as well. Wipe them before freeing.
		secureZero(credential->CredentialBlob, credential->CredentialBlobSize);
	}
	CredFree(credentials);
	return ids;
}

} // namespace rd
