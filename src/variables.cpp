#include "variables.hpp"
#include <algorithm>

namespace banshou {
namespace {
constexpr size_t kHeaderSize = 16;
constexpr size_t kMaxArchiveCount = 65536;
const char kMagic[8] = {'C', 'C', 'Z', 'B', 'O', 'O', 'L', '1'};

uint32_t crcTableEntry(uint32_t index) {
  uint32_t value = index;
  for (int bit = 0; bit < 8; ++bit)
    value = (value & 1) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
  return value;
}
} // namespace

uint32_t crc32(const Bytes &data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t byte : data)
    crc = crcTableEntry((crc ^ byte) & 0xFF) ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

Bytes encodeBooleanArchive(const std::vector<bool> &values, size_t count) {
  if (count == 0 || count > kMaxArchiveCount)
    throw Error("布尔导出数量必须在 1..65536");
  if (values.size() < count)
    throw Error("布尔导出值数量不足");
  const size_t payloadSize = (count + 7) / 8;
  Bytes payload(payloadSize, 0);
  for (size_t i = 0; i < count; ++i)
    if (values[i])
      payload[i / 8] |= uint8_t(0x80u >> (i % 8));
  if (count % 8)
    payload.back() &= uint8_t(0xFFu << (8 - count % 8));
  Bytes material(4);
  putNumber(material, 0, 4, count);
  material.insert(material.end(), payload.begin(), payload.end());
  const uint32_t checksum = crc32(material);
  Bytes out(kHeaderSize + payloadSize, 0);
  std::copy(kMagic, kMagic + 8, out.begin());
  putNumber(out, 8, 4, count);
  putNumber(out, 12, 4, checksum);
  std::copy(payload.begin(), payload.end(), out.begin() + kHeaderSize);
  return out;
}

std::vector<bool> decodeBooleanArchive(const Bytes &data,
                                       size_t expectedCount) {
  if (data.size() < kHeaderSize)
    throw Error("布尔归档头不完整");
  if (!std::equal(kMagic, kMagic + 8, data.begin()))
    throw Error("未知布尔归档格式");
  const auto count = size_t(number(data, 8, 4));
  if (count == 0 || count > kMaxArchiveCount)
    throw Error("布尔归档数量越界");
  if (expectedCount && count != expectedCount)
    throw Error("布尔归档数量与当前原生可写数量不符");
  const size_t payloadSize = (count + 7) / 8;
  if (data.size() != kHeaderSize + payloadSize)
    throw Error("布尔归档长度不符或存在额外字节");
  Bytes material(4);
  putNumber(material, 0, 4, count);
  material.insert(material.end(), data.begin() + kHeaderSize, data.end());
  if (crc32(material) != uint32_t(number(data, 12, 4)))
    throw Error("布尔归档CRC校验失败");
  if (count % 8 && (data.back() & ((1u << (8 - count % 8)) - 1u)))
    throw Error("布尔归档尾部填充位必须为零");
  std::vector<bool> out(count, false);
  for (size_t i = 0; i < count; ++i)
    out[i] = (data[kHeaderSize + i / 8] & (0x80u >> (i % 8))) != 0;
  return out;
}
} // namespace banshou
