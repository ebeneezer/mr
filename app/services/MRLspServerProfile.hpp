#ifndef MRLSPSERVERPROFILE_HPP
#define MRLSPSERVERPROFILE_HPP

#include "MRLspServiceSession.hpp"

#include "../../ui/MRSyntax.hpp"

#include <string>

namespace mr::services {

[[nodiscard]] bool buildLspServerProfileFromEnvironment(MRLspServerProfile &profile);
[[nodiscard]] bool buildLspServerProfileForLanguage(MRSyntaxLanguage language, const std::string &languageName, MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage);
[[nodiscard]] std::string lspServerProfileArgumentText(const MRLspServerProfile &profile);

} // namespace mr::services

#endif
