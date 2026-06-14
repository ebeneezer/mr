#ifndef MRLSPEDITORSOURCE_HPP
#define MRLSPEDITORSOURCE_HPP

#include "MRWorkspaceServiceContext.hpp"

#include "../../lsp/MRLspDocumentService.hpp"
#include "../../ui/MRSyntax.hpp"

#include <string>

class MRFileEditor;

namespace mr::services {

[[nodiscard]] const char *lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage language) noexcept;
[[nodiscard]] bool buildLspDocumentSourceSnapshotFromEditor(const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, mr::lsp::LspDocumentSourceSnapshot &snapshot, std::string &errorMessage);

} // namespace mr::services

#endif
