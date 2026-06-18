#ifndef MRLSPREFERENCES_HPP
#define MRLSPREFERENCES_HPP

#include "MRLspDefinition.hpp"
#include "MRLspDocumentService.hpp"
#include "MRLspLifecycle.hpp"
#include "MRLspSession.hpp"

#include <string>
#include <vector>

namespace mr::lsp {

struct LspReferencesRequest {
	std::string idText;
	std::string method;
	std::string uri;
	LspTextPosition position;
	bool includeDeclaration = false;
	bool pending = false;
};

struct LspReferencesResult {
	std::string originUri;
	std::vector<LspLocation> locations;
};

class LspReferencesAdapter {
public:
	bool requestReferences(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, bool includeDeclaration, LspReferencesRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspReferencesRequest &request, LspReferencesResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
