#ifndef MRLSPDIAGNOSTICS_HPP
#define MRLSPDIAGNOSTICS_HPP

#include "MRLspDocumentService.hpp"
#include "MRLspSession.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mr::lsp {

struct LspDiagnosticRange {
	int startLine = 0;
	int startCharacter = 0;
	int endLine = 0;
	int endCharacter = 0;
};

struct LspDiagnostic {
	LspDiagnosticRange range;
	int severity = 0;
	std::string message;
	std::string rawJson;
};

struct LspDiagnosticBatch {
	std::string uri;
	std::int64_t version = 0;
	std::vector<LspDiagnostic> diagnostics;
	bool hasVersion = false;
	bool accepted = false;
	bool stale = false;
	bool rejected = false;
};

class LspDiagnosticsAdapter {
public:
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspDiagnosticBatch &batch, std::string &errorMessage) const;
};

} // namespace mr::lsp

#endif
