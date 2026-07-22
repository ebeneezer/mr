#ifndef MRBENTOBOXROLESUPPORT_HPP
#define MRBENTOBOXROLESUPPORT_HPP

#include "MRBentoBox.hpp"

#include <string>
#include <vector>

namespace mr::bento {

constexpr ushort cmPaneRoleAccepted = 0x7A20;
constexpr ushort cmPaneActionAccepted = 0x7A21;
constexpr ushort cmFileComparePaneActionAccepted = 0x7A22;

const char *paneRoleTitle(MRBentoPaneRole role) noexcept;
bool paneRoleIsOutline(MRBentoPaneRole role) noexcept;
bool paneRoleIsDiff(MRBentoPaneRole role) noexcept;
bool paneRoleAllowsMultipleInstances(MRBentoPaneRole role) noexcept;
MRBentoPaneRole paneRoleForTitle(const std::string &title) noexcept;
const MRBentoPaneTitleMenuSpec *paneRoleTitleMenu(MRBentoBoxMode mode) noexcept;
const char *paneActionReplace() noexcept;
const char *fileCompareActionNext() noexcept;
const char *fileCompareActionPrevious() noexcept;
const char *fileCompareActionApply() noexcept;
std::vector<std::string> paneRoleChoices(MRBentoBoxMode mode);
std::vector<std::string> paneActionChoices();
MRBentoPanePlacement panePlacementForAction(const std::string &action) noexcept;

} // namespace mr::bento

#endif
