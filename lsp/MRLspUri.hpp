#ifndef MRLSPURI_HPP
#define MRLSPURI_HPP

#include <string>
#include <string_view>

namespace mr::lsp {

[[nodiscard]] bool pathToFileUri(std::string_view absolutePath, std::string &uri, std::string &errorMessage);
[[nodiscard]] bool fileUriToPath(std::string_view uri, std::string &path, std::string &errorMessage);

} // namespace mr::lsp

#endif
