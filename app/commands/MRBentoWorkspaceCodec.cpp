#include "MRBentoWorkspaceCodec.hpp"

#include "../../ui/MRBentoBox/MRBentoBox.hpp"
#include "../../ui/MRBentoBox/MRBentoBoxRoleSupport.hpp"

#include <cstdlib>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace {

constexpr int kBentoPaneNode = 0;
constexpr int kBentoSplitNode = 1;
constexpr int kBentoHorizontalSplit = 0;
constexpr int kBentoVerticalSplit = 1;

struct DroppedBentoPane {
	int leafId;
	int keeperLeafId;
	MRBentoPaneRole role;
};

std::vector<std::string> splitBentoToken(const std::string &text, char delimiter) {
	std::vector<std::string> parts;
	std::string part;
	std::istringstream input(text);

	while (std::getline(input, part, delimiter)) parts.push_back(part);
	if (!text.empty() && text.back() == delimiter) parts.emplace_back();
	return parts;
}

std::string debuggerHexEncode(const std::string &value) {
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string encoded;

	encoded.reserve(value.size() * 2);
	for (unsigned char ch : value) {
		encoded.push_back(hex[(ch >> 4) & 0x0F]);
		encoded.push_back(hex[ch & 0x0F]);
	}
	return encoded;
}

int debuggerHexDigit(char ch) noexcept {
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	return -1;
}

bool debuggerHexDecode(const std::string &value, std::string &decoded) {
	decoded.clear();
	if ((value.size() & 1U) != 0) return false;
	decoded.reserve(value.size() / 2);
	for (std::size_t index = 0; index < value.size(); index += 2) {
		const int high = debuggerHexDigit(value[index]);
		const int low = debuggerHexDigit(value[index + 1]);

		if (high < 0 || low < 0) return false;
		decoded.push_back(static_cast<char>((high << 4) | low));
	}
	return true;
}

bool parseBentoInt(const std::string &text, int &value) {
	char *end = nullptr;
	const long parsed = std::strtol(text.c_str(), &end, 10);

	if (end == text.c_str() || *end != '\0') return false;
	if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return false;
	value = static_cast<int>(parsed);
	return true;
}

bool parseBentoPrefixedInt(const std::string &text, const char *prefix, int &value) {
	const std::string expected(prefix);

	if (text.rfind(expected, 0) != 0) return false;
	return parseBentoInt(text.substr(expected.size()), value);
}

const MRBentoWorkspaceLeaf *bentoLeafForId(const MRBentoWorkspaceSnapshot &snapshot, int leafId) noexcept {
	for (const MRBentoWorkspaceLeaf &leaf : snapshot.leaves)
		if (leaf.id == leafId) return &leaf;
	return nullptr;
}

bool containsBentoLeafId(const std::vector<int> &leafIds, int leafId) noexcept {
	for (int knownLeafId : leafIds)
		if (knownLeafId == leafId) return true;
	return false;
}

const DroppedBentoPane *droppedBentoPaneForLeaf(const std::vector<DroppedBentoPane> &droppedPanes, int leafId) noexcept {
	for (const DroppedBentoPane &dropped : droppedPanes)
		if (dropped.leafId == leafId) return &dropped;
	return nullptr;
}

bool collectReachableBentoLayout(const MRBentoWorkspaceSnapshot &snapshot, std::vector<int> &reachableLeafIds) {
	std::vector<bool> visited(snapshot.nodes.size(), false);
	std::vector<int> stack;

	if (snapshot.rootNode < 0 || snapshot.rootNode >= static_cast<int>(snapshot.nodes.size())) return false;
	stack.push_back(snapshot.rootNode);
	while (!stack.empty()) {
		const int nodeIndex = stack.back();
		stack.pop_back();
		if (nodeIndex < 0 || nodeIndex >= static_cast<int>(snapshot.nodes.size())) return false;
		if (visited[static_cast<std::size_t>(nodeIndex)]) return false;
		visited[static_cast<std::size_t>(nodeIndex)] = true;
		const MRBentoWorkspaceNode &node = snapshot.nodes[static_cast<std::size_t>(nodeIndex)];
		if (node.kind == kBentoPaneNode) {
			if (bentoLeafForId(snapshot, node.leafId) == nullptr || containsBentoLeafId(reachableLeafIds, node.leafId)) return false;
			reachableLeafIds.push_back(node.leafId);
			continue;
		}
		if (node.kind != kBentoSplitNode || (node.orientation != kBentoHorizontalSplit && node.orientation != kBentoVerticalSplit)) return false;
		if (node.firstChild == nodeIndex || node.secondChild == nodeIndex) return false;
		stack.push_back(node.secondChild);
		stack.push_back(node.firstChild);
	}
	return !reachableLeafIds.empty();
}

int cloneCanonicalBentoNode(const MRBentoWorkspaceSnapshot &source, int nodeIndex, const std::vector<DroppedBentoPane> &droppedPanes, std::vector<MRBentoWorkspaceNode> &nodes) {
	const MRBentoWorkspaceNode &sourceNode = source.nodes[static_cast<std::size_t>(nodeIndex)];

	if (sourceNode.kind == kBentoPaneNode) {
		if (droppedBentoPaneForLeaf(droppedPanes, sourceNode.leafId) != nullptr) return -1;
		MRBentoWorkspaceNode node = sourceNode;
		node.firstChild = -1;
		node.secondChild = -1;
		nodes.push_back(node);
		return static_cast<int>(nodes.size()) - 1;
	}
	const int firstChild = cloneCanonicalBentoNode(source, sourceNode.firstChild, droppedPanes, nodes);
	const int secondChild = cloneCanonicalBentoNode(source, sourceNode.secondChild, droppedPanes, nodes);
	if (firstChild < 0) return secondChild;
	if (secondChild < 0) return firstChild;
	MRBentoWorkspaceNode node = sourceNode;
	node.firstChild = firstChild;
	node.secondChild = secondChild;
	node.leafId = -1;
	nodes.push_back(node);
	return static_cast<int>(nodes.size()) - 1;
}

bool normalizeBentoSnapshotForBootstrap(MRBentoWorkspaceSnapshot &snapshot, std::vector<std::string> *bootstrapLogMessages) {
	std::vector<int> reachableLeafIds;
	std::vector<DroppedBentoPane> droppedPanes;
	std::vector<const MRBentoWorkspaceLeaf *> keepers;
	MRBentoWorkspaceSnapshot normalized;

	for (std::size_t i = 0; i < snapshot.leaves.size(); ++i)
		for (std::size_t j = 0; j < i; ++j)
			if (snapshot.leaves[i].id == snapshot.leaves[j].id) return false;
	if (!collectReachableBentoLayout(snapshot, reachableLeafIds)) return false;
	for (const MRBentoWorkspaceLeaf &leaf : snapshot.leaves) {
		if (!containsBentoLeafId(reachableLeafIds, leaf.id) || mr::bento::paneRoleAllowsMultipleInstances(leaf.role)) continue;
		const MRBentoWorkspaceLeaf *keeper = nullptr;
		for (const MRBentoWorkspaceLeaf *known : keepers)
			if (known != nullptr && known->role == leaf.role) {
				keeper = known;
				break;
			}
		if (keeper == nullptr) {
			keepers.push_back(&leaf);
			continue;
		}
		droppedPanes.push_back(DroppedBentoPane{leaf.id, keeper->id, leaf.role});
	}
	normalized.mode = snapshot.mode;
	normalized.rootNode = cloneCanonicalBentoNode(snapshot, snapshot.rootNode, droppedPanes, normalized.nodes);
	if (normalized.rootNode < 0) return false;
	for (const MRBentoWorkspaceLeaf &leaf : snapshot.leaves)
		if (containsBentoLeafId(reachableLeafIds, leaf.id) && droppedBentoPaneForLeaf(droppedPanes, leaf.id) == nullptr) normalized.leaves.push_back(leaf);

	normalized.activeLeafId = snapshot.activeLeafId;
	normalized.maximizedLeafId = snapshot.maximizedLeafId;
	if (const DroppedBentoPane *dropped = droppedBentoPaneForLeaf(droppedPanes, normalized.activeLeafId)) normalized.activeLeafId = dropped->keeperLeafId;
	if (const DroppedBentoPane *dropped = droppedBentoPaneForLeaf(droppedPanes, normalized.maximizedLeafId)) normalized.maximizedLeafId = dropped->keeperLeafId;
	if (!containsBentoLeafId(reachableLeafIds, normalized.activeLeafId) || droppedBentoPaneForLeaf(droppedPanes, normalized.activeLeafId) != nullptr) normalized.activeLeafId = 0;
	if (!containsBentoLeafId(reachableLeafIds, normalized.maximizedLeafId) || droppedBentoPaneForLeaf(droppedPanes, normalized.maximizedLeafId) != nullptr) normalized.maximizedLeafId = -1;

	snapshot = std::move(normalized);
	if (bootstrapLogMessages != nullptr)
		for (const DroppedBentoPane &dropped : droppedPanes)
			bootstrapLogMessages->push_back("Workspace bootstrap dropped duplicate Bento pane role=" + std::string(mr::bento::paneRoleTitle(dropped.role)) + " leaf=" + std::to_string(dropped.leafId) + " keeper=" + std::to_string(dropped.keeperLeafId) + ".");
	return true;
}

} // namespace

namespace mr {
namespace workspace {

std::string encodeBentoSnapshot(const MRBentoWorkspaceSnapshot &snapshot) {
	std::ostringstream out;

	out << "v1.6";
	out << ",m:" << static_cast<int>(snapshot.mode);
	out << ",r:" << snapshot.rootNode;
	out << ",a:" << snapshot.activeLeafId;
	out << ",x:" << snapshot.maximizedLeafId;
	out << ",n:";
	for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
		const MRBentoWorkspaceNode &node = snapshot.nodes[i];
		if (i != 0) out << ".";
		out << node.kind << ":" << node.orientation << ":" << node.dividerPosition << ":" << node.firstChild << ":" << node.secondChild << ":" << node.leafId;
	}
	out << ",l:";
	for (std::size_t i = 0; i < snapshot.leaves.size(); ++i) {
		const MRBentoWorkspaceLeaf &leaf = snapshot.leaves[i];
		if (i != 0) out << ".";
		out << leaf.id << ":" << static_cast<int>(leaf.role) << ":" << (leaf.visible ? 1 : 0) << ":" << leaf.widgetMask;
	}
	return out.str();
}

bool parseBentoSnapshot(const std::string &token, MRBentoWorkspaceSnapshot &snapshot, std::vector<std::string> *bootstrapLogMessages) {
	std::vector<std::string> fields = splitBentoToken(token, ',');
	int mode = 0;

	if (fields.size() != 7) return false;
	const bool hasWidgetMask = fields[0] == "v1.5" || fields[0] == "v1.6";
	const bool hasProgramTerminal = fields[0] == "v1.6";
	if (!hasWidgetMask && fields[0] != "v1") return false;
	if (!parseBentoPrefixedInt(fields[1], "m:", mode)) return false;
	if (!parseBentoPrefixedInt(fields[2], "r:", snapshot.rootNode)) return false;
	if (!parseBentoPrefixedInt(fields[3], "a:", snapshot.activeLeafId)) return false;
	if (!parseBentoPrefixedInt(fields[4], "x:", snapshot.maximizedLeafId)) return false;
	if (mode < bbmToolWorkspace || mode > bbmFileCompare) return false;
	snapshot.mode = static_cast<MRBentoBoxMode>(mode);

	if (fields[5].rfind("n:", 0) != 0 || fields[6].rfind("l:", 0) != 0) return false;
	snapshot.nodes.clear();
	snapshot.leaves.clear();

	for (const std::string &nodeText : splitBentoToken(fields[5].substr(2), '.')) {
		std::vector<std::string> values = splitBentoToken(nodeText, ':');
		MRBentoWorkspaceNode node;

		if (values.size() != 6) return false;
		if (!parseBentoInt(values[0], node.kind)) return false;
		if (!parseBentoInt(values[1], node.orientation)) return false;
		if (!parseBentoInt(values[2], node.dividerPosition)) return false;
		if (!parseBentoInt(values[3], node.firstChild)) return false;
		if (!parseBentoInt(values[4], node.secondChild)) return false;
		if (!parseBentoInt(values[5], node.leafId)) return false;
		snapshot.nodes.push_back(node);
	}
	for (const std::string &leafText : splitBentoToken(fields[6].substr(2), '.')) {
		std::vector<std::string> values = splitBentoToken(leafText, ':');
		MRBentoWorkspaceLeaf leaf;
		int role = 0;
		int visible = 0;
		int widgetMask = 0;

		if (values.size() != (hasWidgetMask ? 4U : 3U)) return false;
		if (!parseBentoInt(values[0], leaf.id)) return false;
		if (!parseBentoInt(values[1], role)) return false;
		if (!parseBentoInt(values[2], visible)) return false;
		if (role < bprSource || (role > bprExtensionLast && !(hasProgramTerminal && role == bprProgramTerminal)) || (visible != 0 && visible != 1)) return false;
		leaf.role = static_cast<MRBentoPaneRole>(role);
		leaf.visible = visible != 0;
		if (hasWidgetMask) {
			if (!parseBentoInt(values[3], widgetMask) || widgetMask < 0 || !MRBentoPaneSpec::widgetMaskIsValid(static_cast<std::uint32_t>(widgetMask))) return false;
			leaf.widgetMask = static_cast<std::uint32_t>(widgetMask);
		} else
			leaf.widgetMask = MRBentoPaneSpec::defaultWidgetMask(leaf.role);
		snapshot.leaves.push_back(leaf);
	}
	return !snapshot.nodes.empty() && !snapshot.leaves.empty() && normalizeBentoSnapshotForBootstrap(snapshot, bootstrapLogMessages);
}

std::string encodeMacroDebuggerConfiguration(const MRMacroDebuggerWorkspaceConfiguration &configuration) {
	std::ostringstream out;

	out << "v2";
	out << ",k:" << debuggerHexEncode(configuration.macroKey);
	out << ",n:" << debuggerHexEncode(configuration.macroName);
	out << ",i:" << debuggerHexEncode(configuration.sourceIdentity);
	out << ",p:" << debuggerHexEncode(configuration.sourcePath);
	out << ",b:";
	for (std::size_t index = 0; index < configuration.breakpoints.size(); ++index) {
		const MRMacroDebuggerWorkspaceBreakpoint &breakpoint = configuration.breakpoints[index];

		if (index != 0) out << ".";
		out << debuggerHexEncode(breakpoint.macroKey) << ":" << debuggerHexEncode(breakpoint.sourceIdentity) << ":" << breakpoint.line << ":" << (breakpoint.enabled ? 1 : 0) << ":" << debuggerHexEncode(breakpoint.conditionText);
	}
	out << ",w:";
	for (std::size_t index = 0; index < configuration.watches.size(); ++index) {
		const MRMacroDebuggerWorkspaceWatch &watch = configuration.watches[index];

		if (index != 0) out << ".";
		out << debuggerHexEncode(watch.expression) << ":" << (watch.enabled ? 1 : 0);
	}
	return out.str();
}

bool parseMacroDebuggerConfiguration(const std::string &token, MRMacroDebuggerWorkspaceConfiguration &configuration) {
	const std::vector<std::string> fields = splitBentoToken(token, ',');
	const bool legacy = !fields.empty() && fields[0] == "v1";

	configuration = MRMacroDebuggerWorkspaceConfiguration();
	if (legacy) {
		if (fields.size() != 5 || fields[1].rfind("k:", 0) != 0 || fields[2].rfind("n:", 0) != 0 || fields[3].rfind("b:", 0) != 0 || fields[4].rfind("w:", 0) != 0) return false;
		if (!debuggerHexDecode(fields[1].substr(2), configuration.macroKey) || !debuggerHexDecode(fields[2].substr(2), configuration.macroName) || configuration.macroKey.empty()) return false;
	} else {
		if (fields.size() != 7 || fields.empty() || fields[0] != "v2" || fields[1].rfind("k:", 0) != 0 || fields[2].rfind("n:", 0) != 0 || fields[3].rfind("i:", 0) != 0 || fields[4].rfind("p:", 0) != 0 || fields[5].rfind("b:", 0) != 0 || fields[6].rfind("w:", 0) != 0) return false;
		if (!debuggerHexDecode(fields[1].substr(2), configuration.macroKey) || !debuggerHexDecode(fields[2].substr(2), configuration.macroName) ||
		    !debuggerHexDecode(fields[3].substr(2), configuration.sourceIdentity) || !debuggerHexDecode(fields[4].substr(2), configuration.sourcePath) || configuration.macroKey.empty())
			return false;
	}
	const std::string breakpointField = fields[legacy ? 3 : 5].substr(2);
	const std::string watchField = fields[legacy ? 4 : 6].substr(2);
	if (!breakpointField.empty())
		for (const std::string &entry : splitBentoToken(breakpointField, '.')) {
			const std::vector<std::string> values = splitBentoToken(entry, ':');
			MRMacroDebuggerWorkspaceBreakpoint breakpoint;
			int enabled = 0;

			if (legacy) {
				if (values.size() != 3 || !debuggerHexDecode(values[0], breakpoint.macroKey) || !parseBentoInt(values[1], breakpoint.line) || !parseBentoInt(values[2], enabled)) return false;
			} else {
				if (values.size() != 5 || !debuggerHexDecode(values[0], breakpoint.macroKey) || !debuggerHexDecode(values[1], breakpoint.sourceIdentity) ||
				    !parseBentoInt(values[2], breakpoint.line) || !parseBentoInt(values[3], enabled) || !debuggerHexDecode(values[4], breakpoint.conditionText))
					return false;
			}
			if (breakpoint.macroKey.empty() || breakpoint.line <= 0 || (enabled != 0 && enabled != 1)) return false;
			breakpoint.enabled = enabled != 0;
			configuration.breakpoints.push_back(breakpoint);
		}
	if (!watchField.empty())
		for (const std::string &entry : splitBentoToken(watchField, '.')) {
			const std::vector<std::string> values = splitBentoToken(entry, ':');
			MRMacroDebuggerWorkspaceWatch watch;
			int enabled = 0;

			if (values.size() != 2 || !debuggerHexDecode(values[0], watch.expression) || !parseBentoInt(values[1], enabled) || watch.expression.empty() || (enabled != 0 && enabled != 1)) return false;
			watch.enabled = enabled != 0;
			configuration.watches.push_back(watch);
		}
	return true;
}

} // namespace workspace
} // namespace mr
