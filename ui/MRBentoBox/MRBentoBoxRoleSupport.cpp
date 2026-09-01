#include "MRBentoBoxRoleSupport.hpp"

namespace {

static const char *kPaneActionReplace = "replace";
static const char *kPaneActionSplitRight = "split \xC4";
static const char *kPaneActionSplitDown = "split \xB3";
static const char *kFileCompareActionNext = "next diff";
static const char *kFileCompareActionPrevious = "prev diff";
static const char *kFileCompareActionApply = "apply diff";
static const MRBentoPaneTitleMenuSpec kPaneRoleTitleMenu{"role"};

struct BentoPaneActionDescriptor {
	const char *action;
	MRBentoPanePlacement placement;
};

static const BentoPaneActionDescriptor kPaneActions[] = {
	{kPaneActionReplace, bppReplace},
	{kPaneActionSplitRight, bppSplitDown},
	{kPaneActionSplitDown, bppSplitRight},
};

struct BentoPaneRoleDescriptor {
	MRBentoPaneRole role;
	const char *title;
	bool listed;
	bool multipleInstances;
};

static const BentoPaneRoleDescriptor kPaneRoles[] = {
	{bprSource, "Source", false, false},
	{bprCompilerOutput, "Compiler Output", true, false},
	{bprAppOutput, "App Output", true, false},
	{bprProblems, "Problems", true, false},
	{bprDebuggerOutput, "Debugger Output", true, false},
	{bprWatches, "Watches", true, false},
	{bprVariables, "Variables", true, false},
	{bprStructure, "Structure", true, false},
	{bprFunctions, "Functions", true, false},
	{bprSplitEditor, "Split editor", true, true},
	{bprDiffOriginal, "Diff Original", true, false},
	{bprDiffCompare, "Diff Compare", true, false},
	{bprProgramTerminal, "Program Terminal", true, false},
};

} // namespace

MRBentoPaneSpec::MRBentoPaneSpec() noexcept : role(bprCompilerOutput), bufferPolicy(bpbOwnBuffer), readOnly(true), widgetMask(bpwNone), suppressMiniMap(true), suppressWordWrap(true), scrollBarsAlwaysVisible(true), titleMenu(&kPaneRoleTitleMenu) {
}

MRBentoPaneSpec::MRBentoPaneSpec(MRBentoPaneRole aRole, MRBentoPaneBufferPolicy aBufferPolicy, bool aReadOnly, bool aSuppressMiniMap, bool aSuppressWordWrap, bool aScrollBarsAlwaysVisible, const MRBentoPaneTitleMenuSpec *aTitleMenu) noexcept
    : role(aRole), bufferPolicy(aBufferPolicy), readOnly(aReadOnly), widgetMask(defaultWidgetMask(aRole)), suppressMiniMap(aSuppressMiniMap), suppressWordWrap(aSuppressWordWrap), scrollBarsAlwaysVisible(aScrollBarsAlwaysVisible), titleMenu(aTitleMenu) {
	if (aSuppressMiniMap)
		widgetMask &= ~static_cast<std::uint32_t>(bpwMiniMap);
	else
		widgetMask |= static_cast<std::uint32_t>(bpwMiniMap);
}

std::uint32_t MRBentoPaneSpec::defaultWidgetMask(MRBentoPaneRole role) noexcept {
	switch (role) {
		case bprSource:
		case bprSplitEditor:
			return static_cast<std::uint32_t>(bpwFoldGutter) | static_cast<std::uint32_t>(bpwMiniMap);
		case bprDiffCompare:
			return static_cast<std::uint32_t>(bpwMiniMap);
		default:
			return static_cast<std::uint32_t>(bpwNone);
	}
}

bool MRBentoPaneSpec::widgetMaskIsValid(std::uint32_t mask) noexcept {
	const std::uint32_t knownMask = static_cast<std::uint32_t>(bpwFoldGutter) | static_cast<std::uint32_t>(bpwMiniMap);

	return (mask & ~knownMask) == 0;
}

namespace mr::bento {

const char *paneRoleTitle(MRBentoPaneRole role) noexcept {
	for (const BentoPaneRoleDescriptor &descriptor : kPaneRoles)
		if (descriptor.role == role) return descriptor.title;
	return "Pane";
}

bool paneRoleIsOutline(MRBentoPaneRole role) noexcept {
	return role == bprStructure || role == bprFunctions;
}

bool paneRoleIsDiff(MRBentoPaneRole role) noexcept {
	return role == bprDiffOriginal || role == bprDiffCompare;
}

bool paneRoleAllowsMultipleInstances(MRBentoPaneRole role) noexcept {
	for (const BentoPaneRoleDescriptor &descriptor : kPaneRoles)
		if (descriptor.role == role) return descriptor.multipleInstances;
	return false;
}

MRBentoPaneRole paneRoleForTitle(const std::string &title) noexcept {
	for (const BentoPaneRoleDescriptor &descriptor : kPaneRoles)
		if (descriptor.listed && title == descriptor.title) return descriptor.role;
	return bprCompilerOutput;
}

const MRBentoPaneTitleMenuSpec *paneRoleTitleMenu(MRBentoBoxMode mode) noexcept {
	return mode == bbmDocumentViewports ? nullptr : &kPaneRoleTitleMenu;
}

const char *paneActionReplace() noexcept {
	return kPaneActionReplace;
}

const char *fileCompareActionNext() noexcept {
	return kFileCompareActionNext;
}

const char *fileCompareActionPrevious() noexcept {
	return kFileCompareActionPrevious;
}

const char *fileCompareActionApply() noexcept {
	return kFileCompareActionApply;
}

std::vector<std::string> paneRoleChoices(MRBentoBoxMode mode) {
	std::vector<std::string> choices;

	for (const BentoPaneRoleDescriptor &descriptor : kPaneRoles) {
		if (!descriptor.listed) continue;
		if (mode == bbmFileCompare && !paneRoleIsDiff(descriptor.role)) continue;
		if (mode != bbmFileCompare && paneRoleIsDiff(descriptor.role)) continue;
		choices.push_back(descriptor.title);
	}
	return choices;
}

std::vector<std::string> paneActionChoices() {
	std::vector<std::string> choices;

	for (const BentoPaneActionDescriptor &descriptor : kPaneActions)
		choices.push_back(descriptor.action);
	return choices;
}

MRBentoPanePlacement panePlacementForAction(const std::string &action) noexcept {
	for (const BentoPaneActionDescriptor &descriptor : kPaneActions)
		if (action == descriptor.action) return descriptor.placement;
	return bppReplace;
}

} // namespace mr::bento
