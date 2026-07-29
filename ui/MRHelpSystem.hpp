#ifndef MRHELPSYSTEM_HPP
#define MRHELPSYSTEM_HPP

#define Uses_TEvent
#include <tvision/tv.h>

#include <vector>

class MRHelpViewer;

class MRHelpSystem {
  public:
	MRHelpSystem() noexcept;

	[[nodiscard]] bool showTopic(ushort context);
	[[nodiscard]] bool showPreviousTopic();

  private:
	friend class MRHelpViewer;

	[[nodiscard]] bool showTopicWithoutHistory(int context);
	void recordTransition(int fromContext, int toContext);
	[[nodiscard]] bool previousContext(int &context);
	void releaseViewer(MRHelpViewer *viewer) noexcept;

	std::vector<int> history;
	int currentContext;
	bool currentContextValid;
	bool helpOpen;
	MRHelpViewer *activeViewer;
};

[[nodiscard]] bool mrShowProjectHelp(ushort context);
[[nodiscard]] bool mrShowPreviousProjectHelp();

#endif
