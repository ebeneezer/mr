#ifndef TVISION_FINDFRST_H
#define TVISION_FINDFRST_H

#include <dos.h>
#include <memory>
#include <mutex>
#include <string>
#include <tvision/tv.h>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <tvision/compat/windows/windows.h>
#endif

namespace tvision {

// A class implementing the behaviour of findfirst and findnext.
// allocate() assigns a FindFirstRec to the provided find_t struct and sets
// the search filters. get() simply retrieves the FindFirstRec that was
// assigned to a find_t struct. next() performs the actual search and
// automatically updates the find_t struct.

class FindFirstRec {

  public:
	static FindFirstRec *allocate(struct find_t *, unsigned, const char *) noexcept;
	static FindFirstRec *get(struct find_t *) noexcept;

	bool next() noexcept;

  private:
	struct find_t *finfo{nullptr};
	unsigned searchAttr{0};

#ifndef _WIN32
	DIR *dirStream{0};
	std::string searchDir;
	std::string wildcard;
#else
	HANDLE hFindFile{INVALID_HANDLE_VALUE};
	std::string fileName;
#endif // _WIN32

	bool open() noexcept;
	void close() noexcept;
	bool setParameters(unsigned, const char *) noexcept;
	bool attrMatch(unsigned attrib) noexcept;

#ifndef _WIN32
	bool setPath(const char *) noexcept;
	bool matchEntry(struct dirent *) noexcept;

	static bool wildcardMatch(char const *wildcard, char const *filename) noexcept;
	unsigned cvtAttr(const struct stat *st, const char *filename) noexcept;
	static void cvtTime(const struct stat *st, struct find_t *fileinfo) noexcept;
#else
	unsigned cvtAttr(const WIN32_FIND_DATAW *findData, const wchar_t *filename) noexcept;
	static void cvtTime(const WIN32_FIND_DATAW *findData, struct find_t *fileinfo) noexcept;
#endif // _WIN32

	static std::vector<std::unique_ptr<FindFirstRec>> recList;
	static std::unordered_map<struct find_t *, size_t> recIndexByFileInfo;
	static std::mutex recMutex;
};

} // namespace tvision

#endif // TVISION_FINDFRST_H
