#ifndef MRHEXINSPECTOR_HPP
#define MRHEXINSPECTOR_HPP

#include "../MRTextBufferModel.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct MRHexInspectorLine {
	const char *label;
	std::string value;
};

void mrBuildHexInspectorLines(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, bool littleEndian, std::vector<MRHexInspectorLine> &lines);

#endif
