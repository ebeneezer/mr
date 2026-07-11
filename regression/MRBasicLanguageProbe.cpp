#include "../app/commands/MRExternalCommand.hpp"
#include "../config/settings/MRSettingsCompilerProfiles.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../ui/MRFileEditor/MRFileEditor.hpp"
#include "../ui/MRSyntax.hpp"
#include "../ui/MRSyntaxBasic.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct BasicBlockProbe {
	const char *line;
	MRBasicBlockKind kind;
	MRBasicBlockDisposition disposition;
};

struct BasicAutoSetupProbe {
	const char *id;
	const char *toolchain;
	const char *flags;
};

bool hasToken(const MRSyntaxLineResult &result, std::size_t column, std::size_t length, MRSyntaxToken token) {
	for (const MRSyntaxTokenRun &run : result.tokenRuns)
		if (run.column == column && run.length == length && run.token == token) return true;
	return false;
}

bool containsProfile(const std::vector<MRCompilerProfile> &profiles, const char *id, const char *flags) {
	for (const MRCompilerProfile &profile : profiles)
		if (profile.id == id) return profile.buildFlags == flags;
	return false;
}

bool reportFailure(const std::string &message) {
	std::cerr << "BASIC probe failed: " << message << '\n';
	return false;
}

} // namespace

int main() {
	static const BasicBlockProbe blockProbes[] = {
		{"IF ready THEN", MRBasicBlockKind::Conditional, MRBasicBlockDisposition::Open},
		{"ELSEIF retry THEN", MRBasicBlockKind::Conditional, MRBasicBlockDisposition::Continue},
		{"END IF", MRBasicBlockKind::Conditional, MRBasicBlockDisposition::Close},
		{"ENDIF", MRBasicBlockKind::Conditional, MRBasicBlockDisposition::Close},
		{"SELECT CASE value", MRBasicBlockKind::Select, MRBasicBlockDisposition::Open},
		{"CASE 1", MRBasicBlockKind::Select, MRBasicBlockDisposition::Continue},
		{"END SELECT", MRBasicBlockKind::Select, MRBasicBlockDisposition::Close},
		{"FOR index = 1 TO 3", MRBasicBlockKind::Loop, MRBasicBlockDisposition::Open},
		{"NEXT", MRBasicBlockKind::Loop, MRBasicBlockDisposition::Close},
		{"PUBLIC SUB Probe()", MRBasicBlockKind::Procedure, MRBasicBlockDisposition::Open},
		{"END SUB", MRBasicBlockKind::Procedure, MRBasicBlockDisposition::Close},
		{"END", MRBasicBlockKind::Procedure, MRBasicBlockDisposition::Close},
		{"PUBLIC CLASS GambasClass", MRBasicBlockKind::Type, MRBasicBlockDisposition::Open},
		{"END CLASS", MRBasicBlockKind::Type, MRBasicBlockDisposition::Close},
		{"TRY", MRBasicBlockKind::Try, MRBasicBlockDisposition::Open},
		{"FINALLY", MRBasicBlockKind::Try, MRBasicBlockDisposition::Continue},
		{"END TRY", MRBasicBlockKind::Try, MRBasicBlockDisposition::Close},
	};
	static const char *const basicExtensions[] = {"probe.bas", "probe.bi", "probe.bm", "probe.qb", "probe.qbas", "Main.module", "Probe.class"};
	static const BasicAutoSetupProbe autoSetupProbes[] = {
		{"FREEBASIC_SIZE", "FREEBASIC", "-O s -strip"},
		{"QB64PE_DEBUG", "QB64PE", "-x -w"},
		{"GAMBAS_SPEED", "GAMBAS", "-a -x -w"},
	};
	const std::vector<MRCompilerProfile> profiles = detectedCompilerProfiles();
	const MRSyntaxLineResult highlighted = tmrHighlightTextLine(MRSyntaxLanguage::Basic, "SUB Probe() : REM hidden");
	const std::string selectFold = mrBuildFoldTrainingAscii("SELECT CASE value\nCASE 0\nPRINT \"zero\"\nCASE ELSE\nPRINT \"other\"\nEND SELECT\n", MRSyntaxLanguage::Basic);
	MRCompilerProfile freeBasicProfile;
	MRCompilerProfile qb64peProfile;
	MRCompilerProfile gambasProfile;
	std::string commandLine;
	std::string errorText;

	for (const char *path : basicExtensions)
		if (tmrDetectSyntaxLanguage(path) != MRSyntaxLanguage::Basic) return reportFailure(std::string("extension was not detected: ") + path) ? 0 : 1;
	if (tmrClassifySyntaxLanguage("", "", "OPTION EXPLICIT\nSUB Probe()\nEND SUB\n").language != MRSyntaxLanguage::Basic)
		return reportFailure("BASIC source without a file extension was not classified.") ? 0 : 1;
	if (!hasToken(highlighted, 0, 3, MRSyntaxToken::Keyword) || !hasToken(highlighted, 4, 5, MRSyntaxToken::Key) || !hasToken(highlighted, 14, 10, MRSyntaxToken::Comment))
		return reportFailure("highlighting did not classify keyword, callable name and REM comment.") ? 0 : 1;
	if (selectFold.find("\xE2\x95\xAD | SELECT CASE value") == std::string::npos || selectFold.find("\xE2\x94\x9C | CASE 0") == std::string::npos ||
	    selectFold.find("\xE2\x94\x9C | CASE ELSE") == std::string::npos ||
	    selectFold.find("\xE2\x94\x82\xE2\x95\xAD | CASE 0") != std::string::npos || selectFold.find("\xE2\x95\xB0 | END SELECT") == std::string::npos)
		return reportFailure("SELECT CASE folding did not produce one selector span.") ? 0 : 1;
	for (const BasicBlockProbe &probe : blockProbes) {
		const MRBasicBlockLine result = mrBasicClassifyBlockLine(probe.line);
		if (result.kind != probe.kind || result.disposition != probe.disposition)
			return reportFailure(std::string("unexpected block classification: ") + probe.line) ? 0 : 1;
	}
	if (!containsProfile(profiles, "FREEBASIC_DEBUG", "-g -exx -O 0") || !containsProfile(profiles, "FREEBASIC_NORMAL", "-O 2") ||
	    !containsProfile(profiles, "FREEBASIC_SPEED", "-O 3") || !containsProfile(profiles, "FREEBASIC_SIZE", "-O s -strip"))
		return reportFailure("FreeBASIC profiles are incomplete or use unexpected flags.") ? 0 : 1;
	if (!containsProfile(profiles, "GAMBAS_DEBUG", "-a -g -w") || !containsProfile(profiles, "GAMBAS_NORMAL", "-a -w") ||
	    !containsProfile(profiles, "GAMBAS_SPEED", "-a -x -w") || !containsProfile(profiles, "GAMBAS_SIZE", "-a -x -w"))
		return reportFailure("Gambas profiles are incomplete or use unexpected flags.") ? 0 : 1;
	if (!containsProfile(profiles, "QB64PE_DEBUG", "-x -w") || !containsProfile(profiles, "QB64PE_NORMAL", "-x -q") ||
	    !containsProfile(profiles, "QB64PE_SPEED", "-x -q") || !containsProfile(profiles, "QB64PE_SIZE", "-x -q"))
		return reportFailure("QB64-PE profiles are incomplete or use unexpected flags.") ? 0 : 1;
	for (const BasicAutoSetupProbe &probe : autoSetupProbes) {
		MRCompilerProfile profile;
		for (const MRCompilerProfile &candidate : profiles)
			if (candidate.id == probe.id) {
				profile = candidate;
				break;
			}
		if (profile.executablePath.empty() || !autoConfigureCompilerProfileFromExecutable(profile, &errorText) || profile.toolchain != probe.toolchain || profile.buildFlags != probe.flags)
			return reportFailure(std::string("automatic compiler setup did not configure ") + probe.id + ".") ? 0 : 1;
	}
	for (const MRCompilerProfile &profile : profiles)
		if (profile.id == "FREEBASIC_NORMAL") {
			freeBasicProfile = profile;
			break;
		}
	for (const MRCompilerProfile &profile : profiles)
		if (profile.id == "QB64PE_NORMAL") {
			qb64peProfile = profile;
			break;
		}
	for (const MRCompilerProfile &profile : profiles)
		if (profile.id == "GAMBAS_NORMAL") {
			gambasProfile = profile;
			break;
		}
	if (freeBasicProfile.executablePath.empty()) return reportFailure("FreeBASIC profile has no compiler executable.") ? 0 : 1;
	if (!buildCompilerProfileCommandLine(freeBasicProfile, "/tmp/basic_probe.bas", commandLine, &errorText) || commandLine.find(" -x '") == std::string::npos)
		return reportFailure("FreeBASIC command line does not use its executable-output switch.") ? 0 : 1;
	if (qb64peProfile.executablePath.empty()) return reportFailure("QB64-PE profile has no compiler executable.") ? 0 : 1;
	const std::string qb64peDirectory = std::filesystem::canonical(qb64peProfile.executablePath).parent_path().string();
	if (!buildCompilerProfileCommandLine(qb64peProfile, "/tmp/basic_probe.bas", commandLine, &errorText) || commandLine.find("cd '" + qb64peDirectory + "' &&") == std::string::npos || commandLine.find(" -o '") == std::string::npos)
		return reportFailure("QB64-PE command line does not enter its compiler directory and set an output executable.") ? 0 : 1;
	if (gambasProfile.executablePath.empty()) return reportFailure("Gambas profile has no compiler executable.") ? 0 : 1;
	const std::string gambasSource = std::filesystem::absolute("regression/gambas-toolchain-probe/.src/Main.module").string();
	const std::string gambasProject = std::filesystem::path(gambasSource).parent_path().parent_path().string();
	if (!buildCompilerProfileCommandLine(gambasProfile, gambasSource, commandLine, &errorText) || commandLine.find("'" + gambasProject + "'") == std::string::npos || commandLine.find(" -o '") != std::string::npos)
		return reportFailure("Gambas command line does not compile the enclosing project directory.") ? 0 : 1;
	std::cout << "BASIC language and compiler-profile probe passed.\n";
	return 0;
}
