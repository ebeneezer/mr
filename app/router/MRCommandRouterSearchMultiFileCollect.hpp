#ifndef MRCOMMANDROUTERSEARCHMULTIFILECOLLECT_HPP
#define MRCOMMANDROUTERSEARCHMULTIFILECOLLECT_HPP

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../piecetable/MRTextDocument.hpp"
#include "MRCommandRouterSearchMultiFileSession.hpp"

struct MultiFileSearchMemorySource {
	std::string normalizedPath;
	std::size_t documentId = 0;
	std::size_t version = 0;
	mr::editor::ReadSnapshot snapshot;
};

struct MultiFileSearchFinishedPayload final : mr::coprocessor::Payload {
	MultiFileCollectOutcome outcome = MultiFileCollectOutcome::Error;
	std::shared_ptr<MultiFileSearchSession> session;
	std::string errorText;

	MultiFileSearchFinishedPayload(MultiFileCollectOutcome anOutcome, std::shared_ptr<MultiFileSearchSession> aSession, std::string anErrorText)
	    : outcome(anOutcome), session(std::move(aSession)), errorText(std::move(anErrorText)) {
	}
};

std::string normalizedSearchPath(const std::filesystem::path &path);
void captureMultiFileSearchMemorySources(std::vector<MultiFileSearchMemorySource> &outSources);
mr::coprocessor::Result runMultiFileSearchTask(const mr::coprocessor::TaskInfo &info, const MRMultiSearchDialogOptions &options, const std::vector<MultiFileSearchMemorySource> &memorySources, const std::string &pattern, const std::string &replacement, bool replaceMode, bool keepFilesOpen);

#endif
