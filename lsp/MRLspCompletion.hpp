#ifndef MRLSPCOMPLETION_HPP
#define MRLSPCOMPLETION_HPP

#include "MRLspDefinition.hpp"
#include "MRLspDocumentService.hpp"
#include "MRLspLifecycle.hpp"
#include "MRLspSession.hpp"

#include <string>
#include <vector>

namespace mr::lsp {

struct LspCompletionRequest {
	std::string idText;
	std::string method;
	std::string uri;
	LspTextPosition position;
	bool pending = false;
};

struct LspCompletionItem {
	std::string label;
	bool hasKind = false;
	int kind = 0;
	std::string detail;
	std::string insertText;
	bool hasInsertTextFormat = false;
	int insertTextFormat = 1;
	bool hasTextEdit = false;
	LspTextPosition textEditStart;
	LspTextPosition textEditEnd;
	std::string textEditNewText;
};

struct LspCompletionResult {
	std::string uri;
	std::string rawResponseJson;
	std::vector<LspCompletionItem> items;
};

class LspCompletionAdapter {
public:
	bool requestCompletion(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspCompletionRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspCompletionRequest &request, LspCompletionResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
