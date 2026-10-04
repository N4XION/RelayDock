// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#include "legal/legal_documents.h"

#include "utils/i18n.h"

#include <algorithm>

namespace rd {

const std::vector<LegalDocument> &legalDocuments()
{
	// Versions must match the "Version" line in each document. A unit test checks this.
	static const std::vector<LegalDocument> documents = {
		{"terms-of-use", "Terms of Use", "1.0", "terms-of-use.md"},
		{"privacy-policy", "Privacy Policy", "1.2", "privacy-policy.md"},
		{"security-notice", "Security and Credentials Notice", "1.1", "security-notice.md"},
		{"third-party-services", "Third-Party Services Notice", "1.2", "third-party-services.md"},
		{"streaming-disclaimer", "Streaming Disclaimer", "1.0", "streaming-disclaimer.md"},
		{"open-source-licenses", "Open Source Licenses", "1.1", "open-source-licenses.md"},
	};
	return documents;
}

const LegalDocument *findLegalDocument(const std::string &id)
{
	for (const LegalDocument &document : legalDocuments()) {
		if (document.id == id)
			return &document;
	}
	return nullptr;
}

std::string legalAcknowledgement(const LegalDocument &document)
{
	// Open Source Licenses grant rights. There is nothing to agree to, only to review.
	if (document.id == "open-source-licenses")
		return locf("Legal.Acknowledge.Reviewed", "I have reviewed the {0}.", document.title);
	return locf("Legal.Acknowledge.Agree", "I have reviewed and agree to the {0}.", document.title);
}

std::vector<std::string> pendingLegalDocuments(const std::vector<LegalAcceptance> &records)
{
	std::vector<std::string> pending;
	for (const LegalDocument &document : legalDocuments()) {
		const bool accepted = std::any_of(records.begin(), records.end(), [&](const LegalAcceptance &record) {
			return record.documentId == document.id && record.version == document.version;
		});
		if (!accepted)
			pending.push_back(document.id);
	}
	return pending;
}

bool legalComplete(const std::vector<LegalAcceptance> &records)
{
	return pendingLegalDocuments(records).empty();
}

bool recordLegalAcceptance(std::vector<LegalAcceptance> &records, const std::string &documentId,
			   const std::string &acceptedAtUtc, const std::string &appVersion)
{
	const LegalDocument *document = findLegalDocument(documentId);
	if (!document)
		return false;

	std::erase_if(records, [&](const LegalAcceptance &record) { return record.documentId == documentId; });

	LegalAcceptance record;
	record.documentId = document->id;
	record.version = document->version;
	record.acceptedAtUtc = acceptedAtUtc;
	record.appVersion = appVersion;
	records.push_back(std::move(record));
	return true;
}

void pruneLegalRecords(std::vector<LegalAcceptance> &records)
{
	std::erase_if(records, [](const LegalAcceptance &record) { return findLegalDocument(record.documentId) == nullptr; });
}

} // namespace rd
