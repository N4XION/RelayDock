// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

#include "settings/app_config.h"

#include <string>
#include <vector>

namespace rd {

// The legal documents RelayDock shows before the first stream, and the record of which
// versions the user has reviewed and agreed to.
//
// A document's text lives in resources/legal/<file>. Its version lives here. To change a
// document: edit the text, raise the version here and in the document's header, and update
// the "Last updated" line. RelayDock then asks every user to review the new version, because
// the version they agreed to no longer matches.
//
// A record holds the document id, its version, the time and the RelayDock version. It holds
// no credentials and nothing that identifies the user.

struct LegalDocument {
	std::string id;      // Stable id, also the acceptance record key
	std::string title;   // Shown as the page title
	std::string version; // Raise when the text changes in a way users must review
	std::string file;    // File name under resources/legal
};

// Every required document, in the order the onboarding shows them.
const std::vector<LegalDocument> &legalDocuments();

const LegalDocument *findLegalDocument(const std::string &id);

// The sentence next to a document's checkbox, for example
// "I have reviewed and agree to the Terms of Use."
std::string legalAcknowledgement(const LegalDocument &document);

// Ids of documents the user still has to review: never accepted, or accepted in another
// version than the current one.
std::vector<std::string> pendingLegalDocuments(const std::vector<LegalAcceptance> &records);

// True when every required document is accepted in its current version.
bool legalComplete(const std::vector<LegalAcceptance> &records);

// Records that the user reviewed and agreed to the current version of a document. Replaces
// an older record for the same document. Returns false for an unknown id.
bool recordLegalAcceptance(std::vector<LegalAcceptance> &records, const std::string &documentId,
			   const std::string &acceptedAtUtc, const std::string &appVersion);

// Removes records of documents that no longer exist.
void pruneLegalRecords(std::vector<LegalAcceptance> &records);

} // namespace rd
