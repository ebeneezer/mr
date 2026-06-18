#ifndef MRLSPHOVER_HPP
#define MRLSPHOVER_HPP

#include "MRLspDefinition.hpp"
#include "MRLspDocumentService.hpp"
#include "MRLspLifecycle.hpp"
#include "MRLspSession.hpp"

#include <string>

namespace mr::lsp {

struct LspHoverRequest {
	std::string idText;
	std::string method;
	std::string uri;
	LspTextPosition position;
	bool pending = false;
};

struct LspHoverResult {
	std::string uri;
	std::string kind;
	std::string value;
};

class LspHoverAdapter {
public:
	bool requestHover(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspHoverRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspHoverRequest &request, LspHoverResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
