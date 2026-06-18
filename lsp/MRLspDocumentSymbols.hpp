#ifndef MRLSPDOCUMENTSYMBOLS_HPP
#define MRLSPDOCUMENTSYMBOLS_HPP

#include "MRLspDefinition.hpp"
#include "MRLspDocumentService.hpp"
#include "MRLspLifecycle.hpp"
#include "MRLspSession.hpp"

#include <string>
#include <vector>

namespace mr::lsp {

struct LspDocumentSymbol {
	std::string name;
	std::string detail;
	int kind = 0;
	LspLocation location;
	int depth = 0;
};

struct LspDocumentSymbolsRequest {
	std::string idText;
	std::string method;
	std::string uri;
	bool pending = false;
};

struct LspDocumentSymbolsResult {
	std::string originUri;
	std::vector<LspDocumentSymbol> symbols;
};

class LspDocumentSymbolsAdapter {
public:
	bool requestDocumentSymbols(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspDocumentSymbolsRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspDocumentSymbolsRequest &request, LspDocumentSymbolsResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
