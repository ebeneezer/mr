#ifndef MRHEXUTF8_HPP
#define MRHEXUTF8_HPP

#include "../MRTextBufferModel.hpp"

#include <cstddef>

[[nodiscard]] bool mrHexUtf8CodePointAt(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, std::size_t &width) noexcept;

#endif
