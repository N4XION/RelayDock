// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// Every key in this file is made up for the test. None of them belongs to a real account.
//
// The Windows tests write to the real Windows Credential Manager under a prefix that is
// unique to each test run ("RelayDockTest-<uuid>") and delete every entry they create.
// They never read or touch entries saved by an installed RelayDock.
#include <doctest/doctest.h>

#include "security/memory_credential_store.h"
#include "security/redactor.h"
#include "security/secret_string.h"
#include "security/secret_vault.h"
#include "security/windows_credential_store.h"
#include "utils/log.h"
#include "utils/uuid.h"

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using rd::CredentialId;
using rd::CredentialKind;
using rd::CredentialStatus;
using rd::SecretString;

namespace {

// Removes every entry a Windows test store created, even when a check fails mid-test.
struct ScopedWindowsStore {
	rd::WindowsCredentialStore store{"RelayDockTest-" + rd::generateUuid()};

	~ScopedWindowsStore()
	{
		for (const CredentialId &id : store.list())
			store.remove(id);
	}
};

template <class Store> void exerciseStore(Store &store)
{
	const CredentialId key{rd::generateUuid(), CredentialKind::StreamKey};
	const CredentialId password{key.destinationId, CredentialKind::Password};
	const CredentialId other{rd::generateUuid(), CredentialKind::StreamKey};

	SUBCASE("a missing secret reports NotFound")
	{
		SecretString out;
		CHECK(store.read(key, out).status == CredentialStatus::NotFound);
		CHECK_FALSE(store.exists(key));
		CHECK(store.remove(key).status == CredentialStatus::NotFound);
	}

	SUBCASE("write then read returns the same bytes")
	{
		const SecretString secret("live_000000000_TESTONLYnotarealkey0001"); // NOT-REAL: made up for this test
		REQUIRE(store.write(key, secret).ok());
		CHECK(store.exists(key));

		SecretString out;
		REQUIRE(store.read(key, out).ok());
		CHECK(out.equals(secret));
	}

	SUBCASE("a second write replaces the first")
	{
		REQUIRE(store.write(key, SecretString("first-value-0001")).ok());
		REQUIRE(store.write(key, SecretString("second-value-0002")).ok());
		SecretString out;
		REQUIRE(store.read(key, out).ok());
		CHECK(out.reveal() == "second-value-0002");
	}

	SUBCASE("stream key and password of one destination are separate entries")
	{
		REQUIRE(store.write(key, SecretString("the-stream-key-value")).ok());
		REQUIRE(store.write(password, SecretString("the-password-value")).ok());

		SecretString outKey;
		SecretString outPassword;
		REQUIRE(store.read(key, outKey).ok());
		REQUIRE(store.read(password, outPassword).ok());
		CHECK(outKey.reveal() == "the-stream-key-value");
		CHECK(outPassword.reveal() == "the-password-value");
	}

	SUBCASE("remove deletes only the named entry")
	{
		REQUIRE(store.write(key, SecretString("value-for-first")).ok());
		REQUIRE(store.write(other, SecretString("value-for-second")).ok());
		REQUIRE(store.remove(key).ok());

		SecretString out;
		CHECK(store.read(key, out).status == CredentialStatus::NotFound);
		CHECK_FALSE(store.exists(key));
		REQUIRE(store.read(other, out).ok());
		CHECK(out.reveal() == "value-for-second");
	}

	SUBCASE("list returns every stored id and nothing else")
	{
		REQUIRE(store.write(key, SecretString("value-a-0001")).ok());
		REQUIRE(store.write(password, SecretString("value-b-0002")).ok());
		REQUIRE(store.write(other, SecretString("value-c-0003")).ok());

		const std::vector<CredentialId> ids = store.list();
		CHECK(ids.size() == 3);
		CHECK(std::find(ids.begin(), ids.end(), key) != ids.end());
		CHECK(std::find(ids.begin(), ids.end(), password) != ids.end());
		CHECK(std::find(ids.begin(), ids.end(), other) != ids.end());
	}

	SUBCASE("keys with punctuation, spaces and non-ASCII text survive")
	{
		const std::string value = "FB-100-0-Ab?s_bl=1&s_ps=1 caf\xC3\xA9/\\\"'%";
		REQUIRE(store.write(key, SecretString(value)).ok());
		SecretString out;
		REQUIRE(store.read(key, out).ok());
		CHECK(out.reveal() == value);
	}

	SUBCASE("invalid input is rejected before it reaches the backend")
	{
		const CredentialId bad{"not-a-uuid", CredentialKind::StreamKey};
		SecretString out;
		CHECK(store.write(bad, SecretString("value-0001")).status == CredentialStatus::InvalidArgument);
		CHECK(store.read(bad, out).status == CredentialStatus::InvalidArgument);
		CHECK(store.remove(bad).status == CredentialStatus::InvalidArgument);
		CHECK_FALSE(store.exists(bad));

		CHECK(store.write(key, SecretString("")).status == CredentialStatus::InvalidArgument);

		const std::string huge(rd::ICredentialStore::kMaxSecretBytes + 1, 'x');
		CHECK(store.write(key, SecretString(huge)).status == CredentialStatus::TooLarge);
		CHECK_FALSE(store.exists(key));
	}

	SUBCASE("a secret at the size limit is accepted")
	{
		const std::string big(rd::ICredentialStore::kMaxSecretBytes, 'k');
		REQUIRE(store.write(key, SecretString(big)).ok());
		SecretString out;
		REQUIRE(store.read(key, out).ok());
		CHECK(out.size() == big.size());
	}
}

} // namespace

TEST_SUITE("security.secret_string")
{
	TEST_CASE("cannot be copied or converted to a string by accident")
	{
		CHECK_FALSE(std::is_copy_constructible_v<SecretString>);
		CHECK_FALSE(std::is_copy_assignable_v<SecretString>);
		CHECK_FALSE(std::is_convertible_v<SecretString, std::string>);
		CHECK_FALSE(std::is_convertible_v<SecretString, std::string_view>);
		CHECK_FALSE(std::is_convertible_v<SecretString, const char *>);
	}

	TEST_CASE("moving empties the source")
	{
		SecretString a("move-me-secret-value");
		SecretString b(std::move(a));
		CHECK(a.empty());
		CHECK(b.reveal() == "move-me-secret-value");

		SecretString c;
		c = std::move(b);
		CHECK(b.empty());
		CHECK(c.reveal() == "move-me-secret-value");
	}

	TEST_CASE("clear and assign")
	{
		SecretString a("one-secret");
		a.assign("two-secret");
		CHECK(a.reveal() == "two-secret");
		a.clear();
		CHECK(a.empty());
		CHECK(a.size() == 0);
	}

	TEST_CASE("equals compares values")
	{
		CHECK(SecretString("same").equals(SecretString("same")));
		CHECK_FALSE(SecretString("same").equals(SecretString("sama")));
		CHECK_FALSE(SecretString("same").equals(SecretString("same-longer")));
		CHECK(SecretString().equals(SecretString()));
	}

	TEST_CASE("the mask never reflects the secret")
	{
		const std::string mask = rd::secretMask();
		CHECK(mask.size() == 16 * 3); // sixteen three-byte bullets
		CHECK(mask.find("a") == std::string::npos);
	}
}

TEST_SUITE("security.credential_names")
{
	TEST_CASE("target names round-trip and reject foreign entries")
	{
		const CredentialId id{rd::generateUuid(), CredentialKind::Password};
		const std::string target = rd::credentialTargetName("RelayDock", id);
		CHECK(target == "RelayDock:" + id.destinationId + ":password");

		CredentialId parsed;
		REQUIRE(rd::parseCredentialTargetName("RelayDock", target, parsed));
		CHECK(parsed == id);

		CHECK_FALSE(rd::parseCredentialTargetName("RelayDock", "Other:" + id.destinationId + ":password", parsed));
		CHECK_FALSE(rd::parseCredentialTargetName("RelayDock", "RelayDock:" + id.destinationId, parsed));
		CHECK_FALSE(rd::parseCredentialTargetName("RelayDock", "RelayDock:not-a-uuid:password", parsed));
		CHECK_FALSE(rd::parseCredentialTargetName("RelayDock", "RelayDock:" + id.destinationId + ":pin", parsed));
		CHECK_FALSE(rd::parseCredentialTargetName("RelayDock", "RelayDockX:" + id.destinationId + ":password", parsed));
	}
}

TEST_SUITE("security.memory_store")
{
	TEST_CASE("memory credential store")
	{
		rd::MemoryCredentialStore store;
		CHECK_FALSE(store.persistent());
		exerciseStore(store);
	}
}

TEST_SUITE("security.windows_store")
{
	TEST_CASE("Windows Credential Manager store")
	{
		ScopedWindowsStore scoped;
		CHECK(scoped.store.persistent());
		exerciseStore(scoped.store);
	}

	TEST_CASE("secrets persist across store instances")
	{
		ScopedWindowsStore scoped;
		const CredentialId id{rd::generateUuid(), CredentialKind::StreamKey};
		REQUIRE(scoped.store.write(id, SecretString("persisted-test-value-0001")).ok());

		// A new object with the same prefix stands in for the next OBS session.
		rd::WindowsCredentialStore second(scoped.store.prefix());
		SecretString out;
		REQUIRE(second.read(id, out).ok());
		CHECK(out.reveal() == "persisted-test-value-0001");
	}

	TEST_CASE("stores with different prefixes do not see each other")
	{
		ScopedWindowsStore a;
		ScopedWindowsStore b;
		const CredentialId id{rd::generateUuid(), CredentialKind::StreamKey};
		REQUIRE(a.store.write(id, SecretString("only-in-store-a")).ok());

		SecretString out;
		CHECK(b.store.read(id, out).status == CredentialStatus::NotFound);
		CHECK(b.store.list().empty());
		CHECK(a.store.list().size() == 1);
	}

	TEST_CASE("a prefix that is the start of another prefix does not match its entries")
	{
		const std::string base = "RelayDockTest-" + rd::generateUuid();
		rd::WindowsCredentialStore shortStore(base);
		rd::WindowsCredentialStore longStore(base + "x");
		const CredentialId id{rd::generateUuid(), CredentialKind::StreamKey};
		REQUIRE(longStore.write(id, SecretString("belongs-to-long-prefix")).ok());

		CHECK(shortStore.list().empty());
		CHECK(longStore.list().size() == 1);
		CHECK(longStore.remove(id).ok());
	}
}

TEST_SUITE("security.vault")
{
	TEST_CASE("secrets written through the vault are redacted from logs straight away")
	{
		rd::Redactor redactor;
		rd::SecretVault vault(std::make_unique<rd::MemoryCredentialStore>(), redactor);
		const CredentialId id{rd::generateUuid(), CredentialKind::StreamKey};
		const std::string key = "vault-test-key-NOT-REAL-4242";

		REQUIRE(vault.set(id, SecretString(key)).result.ok());
		CHECK(redactor.redact("key is " + key).find(key) == std::string::npos);
	}

	TEST_CASE("secrets read through the vault are registered with the redactor")
	{
		auto store = std::make_unique<rd::MemoryCredentialStore>();
		const CredentialId id{rd::generateUuid(), CredentialKind::StreamKey};
		const std::string key = "preexisting-key-NOT-REAL-9191";
		REQUIRE(store->write(id, SecretString(key)).ok());

		rd::Redactor redactor;
		rd::SecretVault vault(std::move(store), redactor);
		CHECK(redactor.secretCount() == 0);

		vault.primeRedactor();
		CHECK(redactor.redact(key).find(key) == std::string::npos);
	}

	TEST_CASE("when the system store fails the secret stays in memory for the session")
	{
		auto store = std::make_unique<rd::MemoryCredentialStore>();
		rd::MemoryCredentialStore *raw = store.get();
		rd::Redactor redactor;
		rd::SecretVault vault(std::move(store), redactor);
		raw->failWith(CredentialStatus::Unavailable);

		const CredentialId id{rd::generateUuid(), CredentialKind::StreamKey};
		const rd::SecretVault::SetResult result = vault.set(id, SecretString("session-only-key-0001"));
		CHECK_FALSE(result.result.ok());
		CHECK(result.keptForSessionOnly);
		CHECK(vault.has(id));
		CHECK(vault.sessionOnly(id));

		SecretString out;
		REQUIRE(vault.get(id, out).ok());
		CHECK(out.reveal() == "session-only-key-0001");
	}

	TEST_CASE("a later successful save replaces the session copy")
	{
		auto store = std::make_unique<rd::MemoryCredentialStore>();
		rd::MemoryCredentialStore *raw = store.get();
		rd::Redactor redactor;
		rd::SecretVault vault(std::move(store), redactor);
		const CredentialId id{rd::generateUuid(), CredentialKind::StreamKey};

		raw->failWith(CredentialStatus::Unavailable);
		vault.set(id, SecretString("old-session-value"));
		raw->failWith(CredentialStatus::Ok);
		REQUIRE(vault.set(id, SecretString("new-saved-value")).result.ok());

		CHECK_FALSE(vault.sessionOnly(id));
		SecretString out;
		REQUIRE(vault.get(id, out).ok());
		CHECK(out.reveal() == "new-saved-value");
	}

	TEST_CASE("removeDestination removes the key and the password")
	{
		rd::Redactor redactor;
		rd::SecretVault vault(std::make_unique<rd::MemoryCredentialStore>(), redactor);
		const std::string destination = rd::generateUuid();
		const std::string keep = rd::generateUuid();
		vault.set({destination, CredentialKind::StreamKey}, SecretString("key-to-remove-01"));
		vault.set({destination, CredentialKind::Password}, SecretString("password-to-remove-01"));
		vault.set({keep, CredentialKind::StreamKey}, SecretString("key-to-keep-0001"));

		vault.removeDestination(destination);
		CHECK_FALSE(vault.has({destination, CredentialKind::StreamKey}));
		CHECK_FALSE(vault.has({destination, CredentialKind::Password}));
		CHECK(vault.has({keep, CredentialKind::StreamKey}));
	}

	TEST_CASE("removeOrphans keeps secrets of known destinations only")
	{
		rd::Redactor redactor;
		rd::SecretVault vault(std::make_unique<rd::MemoryCredentialStore>(), redactor);
		const std::string known = rd::generateUuid();
		const std::string orphan = rd::generateUuid();
		vault.set({known, CredentialKind::StreamKey}, SecretString("known-key-0001"));
		vault.set({orphan, CredentialKind::StreamKey}, SecretString("orphan-key-0001"));
		vault.set({orphan, CredentialKind::Password}, SecretString("orphan-password-0001"));

		CHECK(vault.removeOrphans({known}) == 2);
		CHECK(vault.has({known, CredentialKind::StreamKey}));
		CHECK_FALSE(vault.has({orphan, CredentialKind::StreamKey}));
	}

	TEST_CASE("removeAll empties the vault")
	{
		rd::Redactor redactor;
		rd::SecretVault vault(std::make_unique<rd::MemoryCredentialStore>(), redactor);
		vault.set({rd::generateUuid(), CredentialKind::StreamKey}, SecretString("key-one-0001"));
		vault.set({rd::generateUuid(), CredentialKind::StreamKey}, SecretString("key-two-0002"));
		CHECK(vault.removeAll() == 2);
		CHECK(vault.list().empty());
	}

	TEST_CASE("a failed save is logged without the secret")
	{
		std::vector<std::string> lines;
		rd::setLogSink([&lines](rd::LogLevel, const std::string &line) { lines.push_back(line); });

		auto store = std::make_unique<rd::MemoryCredentialStore>();
		store->failWith(CredentialStatus::AccessDenied);
		rd::SecretVault vault(std::move(store), rd::globalRedactor());
		const std::string key = "do-not-log-this-key-7373";
		vault.set({rd::generateUuid(), CredentialKind::StreamKey}, SecretString(key));

		rd::setLogSink(nullptr);
		rd::globalRedactor().clearSecrets();
		rd::clearRecentLogLines();

		REQUIRE_FALSE(lines.empty());
		for (const std::string &line : lines)
			CHECK(line.find(key) == std::string::npos);
	}
}
