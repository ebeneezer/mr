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
	std::string triggerCharacter;
	bool hasTriggerCharacter = false;
	bool pending = false;
};

struct LspCompletionItem {
	std::string rawJson;
	std::string label;
	bool hasKind = false;
	int kind = 0;
	std::string detail;
	std::string documentation;
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

struct LspCompletionResolveRequest {
	std::string idText;
	std::string method;
	std::string label;
	bool pending = false;
};

struct LspCompletionResolveResult {
	std::string rawResponseJson;
	LspCompletionItem item;
};

class LspCompletionAdapter {
public:
	bool requestCompletion(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, const std::string &triggerCharacter, LspCompletionRequest &request, std::string &errorMessage);
	bool consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspCompletionRequest &request, LspCompletionResult &result, bool &accepted, std::string &errorMessage);
	bool requestResolve(LspLifecycle &lifecycle, const LspCompletionItem &item, LspCompletionResolveRequest &request, std::string &errorMessage);
	bool consumeResolve(const LspInboundMessage &message, LspCompletionResolveRequest &request, LspCompletionResolveResult &result, bool &accepted, std::string &errorMessage);

private:
	int nextRequestId = 1;
};

} // namespace mr::lsp

#endif
