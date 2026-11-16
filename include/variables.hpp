#pragma once
#include "platform.hpp"
#include <vector>

namespace banshou {
// CCZBOOL1 is a raw packed-bit archive, not a zlib stream.  The payload is
// MSB-first per byte and trailing padding bits must stay zero.
Bytes encodeBooleanArchive(const std::vector<bool> &values, size_t count);
std::vector<bool> decodeBooleanArchive(const Bytes &data,
                                       size_t expectedCount);
uint32_t crc32(const Bytes &data);
} // namespace banshou
