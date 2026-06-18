#ifndef MRLSPDEFINITION_HPP
#define MRLSPDEFINITION_HPP

#include "MRLspDocumentService.hpp"
#include "MRLspLifecycle.hpp"
#include "MRLspSession.hpp"

#include <string>
#include <vector>

namespace mr::lsp {

struct LspTextPosition {
	int line = 0;
	int character = 0;
};

struct LspLocation {
	std::string uri;
	LspTextPosition start;
	LspTextPosition end;
};

struct LspDefinitionRequest {
	std::string idText;
	std::string method;
	std::string uri;
	LspTextPosition position;
	bool pending = false;
};

struct LspDefinitionResult {
	std::string originUri;
	std::vector<LspLocation> locations;
};

class LspDefinitionAdapter {
public:
	bool requestDefinition(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspDefinitionRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspDefinitionRequest &request, LspDefinitionResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
