#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "mrmac.h"
#include "mrvm.hpp"

static bool readText(const char *path, std::string &out) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	std::ostringstream buf;

	if (!in)
		return false;
	buf << in.rdbuf();
	if (!in.good() && !in.eof())
		return false;
	out = buf.str();
	return true;
}

int main(int argc, char **argv) {
	int exitCode = 0;

	if (argc < 2) {
		std::cerr << "usage: misc/mr_stage_profile_probe <macro.mrmac> [...]\n";
		return 2;
	}

	for (int i = 1; i < argc; ++i) {
		std::string source;
		std::size_t bytecodeSize = 0;
		unsigned char *bytecode = NULL;
		MRMacroExecutionProfile profile;
		std::vector<std::string> unsupported;

		if (!readText(argv[i], source)) {
			std::cout << argv[i] << ": read_error\n";
			exitCode = 1;
			continue;
		}

		bytecode = compile_macro_code(source.c_str(), &bytecodeSize);
		if (bytecode == NULL) {
			const char *err = get_last_compile_error();
			std::cout << argv[i] << ": compile_error=" << (err != NULL ? err : "unknown") << "\n";
			exitCode = 1;
			continue;
		}

		profile = mrvmAnalyzeBytecode(bytecode, bytecodeSize);
		unsupported = mrvmUnsupportedStagedSymbols(profile);
		std::cout << argv[i] << ": canBackground=" << (mrvmCanRunInBackground(profile) ? 1 : 0)
		          << " canStage=" << (mrvmCanRunStagedInBackground(profile) ? 1 : 0)
		          << " unsupported=";
		if (unsupported.empty())
			std::cout << "<none>";
		else {
			for (std::size_t j = 0; j < unsupported.size(); ++j) {
				if (j != 0)
					std::cout << ",";
				std::cout << unsupported[j];
			}
		}
		std::cout << "\n";
		std::free(bytecode);
	}
	return exitCode;
}
