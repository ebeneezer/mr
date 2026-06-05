#ifndef MROUTLINEMODEL_HPP
#define MROUTLINEMODEL_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

enum MROutlineKind : unsigned char {
	mrokUnknown = 0,
	mrokModule,
	mrokNamespace,
	mrokClass,
	mrokMethod,
	mrokFunction,
	mrokSection,
	mrokMacro,
	mrokTarget,
	mrokBlock
};

enum MROutlineSource : unsigned char {
	mrosFold = 0,
	mrosLsp
};

enum MROutlineConfidence : unsigned char {
	mrocStructural = 0,
	mrocHeuristic
};

enum MROutlineView : unsigned char {
	mrovStructure = 0,
	mrovFunctions
};

struct MROutlineRequest {
	MROutlineView view = mrovStructure;
	bool allowPartial = true;
};

struct MROutlinePosition {
	std::size_t line = 0;
	std::size_t column = 0;
	std::size_t offset = 0;
};

struct MROutlineRange {
	MROutlinePosition start;
	MROutlinePosition end;
};

struct MROutlineNode {
	static constexpr std::uint32_t npos = std::numeric_limits<std::uint32_t>::max();

	std::uint32_t parent = npos;
	std::uint32_t firstChild = npos;
	std::uint32_t nextSibling = npos;
	std::uint32_t nameOffset = 0;
	std::uint16_t nameLength = 0;
	std::uint32_t detailOffset = 0;
	std::uint16_t detailLength = 0;
	MROutlineKind kind = mrokUnknown;
	MROutlineSource source = mrosFold;
	MROutlineConfidence confidence = mrocStructural;
	MROutlineRange range;
	MROutlineRange selectionRange;
};

struct MROutlineSnapshot {
	std::size_t documentId = 0;
	std::size_t version = 0;
	std::size_t topLine = 0;
	std::size_t bottomLine = 0;
	bool complete = false;
	std::vector<MROutlineNode> nodes;
	std::string textPool;
};

#endif
