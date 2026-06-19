#ifndef MRLSPDOCUMENTHIGHLIGHT_HPP
#define MRLSPDOCUMENTHIGHLIGHT_HPP

#include "MRLspDefinition.hpp"
#include "MRLspDocumentService.hpp"
#include "MRLspLifecycle.hpp"
#include "MRLspSession.hpp"

#include <string>
#include <vector>

namespace mr::lsp {

struct LspDocumentHighlightRequest {
	std::string idText;
	std::string method;
	std::string uri;
	LspTextPosition position;
	bool pending = false;
};

struct LspDocumentHighlightRange {
	LspTextPosition start;
	LspTextPosition end;
	bool hasKind = false;
	int kind = 1;
};

struct LspDocumentHighlightResult {
	std::string uri;
	std::vector<LspDocumentHighlightRange> highlights;
};

class LspDocumentHighlightAdapter {
public:
	bool requestDocumentHighlight(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspDocumentHighlightRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspDocumentHighlightRequest &request, LspDocumentHighlightResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
