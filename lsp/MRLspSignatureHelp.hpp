#ifndef MRLSPSIGNATUREHELP_HPP
#define MRLSPSIGNATUREHELP_HPP

#include "MRLspDefinition.hpp"
#include "MRLspDocumentService.hpp"
#include "MRLspLifecycle.hpp"
#include "MRLspSession.hpp"

#include <string>
#include <vector>

namespace mr::lsp {

struct LspSignatureParameter {
	std::string label;
};

struct LspSignatureInformation {
	std::string label;
	std::string documentation;
	std::vector<LspSignatureParameter> parameters;
};

struct LspSignatureHelpRequest {
	std::string idText;
	std::string method;
	std::string uri;
	LspTextPosition position;
	bool pending = false;
};

struct LspSignatureHelpResult {
	std::string uri;
	int activeSignature = 0;
	int activeParameter = 0;
	std::vector<LspSignatureInformation> signatures;
};

class LspSignatureHelpAdapter {
public:
	bool requestSignatureHelp(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspSignatureHelpRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspSignatureHelpRequest &request, LspSignatureHelpResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
