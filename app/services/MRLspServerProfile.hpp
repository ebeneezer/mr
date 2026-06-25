#ifndef MRLSPSERVERPROFILE_HPP
#define MRLSPSERVERPROFILE_HPP

#include "MRLspServiceSession.hpp"

#include "../../ui/MRSyntax.hpp"

#include <string>
#include <vector>

struct MRCompilerProfile;

namespace mr::services {

struct MRLspServerCandidate {
	MRSyntaxLanguage language;
	std::string profileName;
	std::string executableName;
	std::vector<std::string> arguments;
	std::string middlewarePath;
};

[[nodiscard]] bool buildLspServerProfileFromEnvironment(MRLspServerProfile &profile);
[[nodiscard]] bool buildLspServerProfileFromCompilerProfile(const MRCompilerProfile &compilerProfile, MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage);
[[nodiscard]] bool buildLspServerProfileForLanguage(MRSyntaxLanguage language, const std::string &languageName, MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage);
[[nodiscard]] bool buildLspServerProfileForLanguageWithCompilerProfile(MRSyntaxLanguage language, const std::string &languageName, const MRCompilerProfile &compilerProfile, MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage);
[[nodiscard]] bool resolveLspServerCandidate(const MRLspServerCandidate &candidate, MRLspServerProfile &profile);
[[nodiscard]] std::vector<MRLspServerCandidate> lspServerCandidatesForLanguage(MRSyntaxLanguage language);
[[nodiscard]] std::vector<MRLspServerCandidate> availableLspServerCandidatesForLanguage(MRSyntaxLanguage language);
[[nodiscard]] std::string lspServerExecutableCandidatesForLanguage(MRSyntaxLanguage language);
[[nodiscard]] std::string lspServerProfileArgumentText(const MRLspServerProfile &profile);

} // namespace mr::services

#endif
