#ifndef MRACQUIREDIALOG_HPP
#define MRACQUIREDIALOG_HPP

#include <tvision/tv.h>

enum class MRAcquireMode : unsigned char {
	OpenFile = 0,
	LoadFile = 1
};

[[nodiscard]] ushort runAcquireDialog(MRAcquireMode mode);

#endif
