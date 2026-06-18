#ifndef MRLSPURI_HPP
#define MRLSPURI_HPP

#include <string>

namespace mr::lsp {

[[nodiscard]] bool pathToFileUri(const std::string &absolutePath, std::string &uri, std::string &errorMessage);
[[nodiscard]] bool fileUriToPath(const std::string &uri, std::string &path, std::string &errorMessage);

} // namespace mr::lsp

#endif
