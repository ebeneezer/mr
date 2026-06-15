#ifndef MRLSPCODEACTION_HPP
#define MRLSPCODEACTION_HPP

#include "MRLspDefinition.hpp"
#include "MRLspDocumentService.hpp"
#include "MRLspLifecycle.hpp"
#include "MRLspSession.hpp"

#include <string>
#include <vector>

namespace mr::lsp {

struct LspCodeActionRange {
	LspTextPosition start;
	LspTextPosition end;
};

struct LspCodeActionRequest {
	std::string idText;
	std::string method;
	std::string uri;
	LspCodeActionRange range;
	std::string diagnosticJson;
	bool pending = false;
};

struct LspCodeActionItem {
	std::string title;
	std::string kind;
	bool hasEdit = false;
	bool hasCommand = false;
	std::string rawJson;
};

struct LspCodeActionResult {
	std::string uri;
	std::vector<LspCodeActionItem> items;
};

class LspCodeActionAdapter {
public:
	bool requestCodeActions(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspCodeActionRange range, const std::string &diagnosticJson, LspCodeActionRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspCodeActionRequest &request, LspCodeActionResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
