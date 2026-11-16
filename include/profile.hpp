#pragma once
#include "image.hpp"
#include <unordered_map>
namespace banshou {
enum class EngineVersion {
  V64 = 0,
  V63MP = 1,
  V63 = 2,
  V62 = 3,
  V61 = 4,
  V65 = 5,
  V66 = 6,
  Special = -1
};
enum class WritePolicy { LegacyDirect };
struct Capability {
  bool readable = false, writable = false;
  std::string state, reason, reasonCode, recipeId;
  std::vector<std::string> evidenceIds;
  std::unordered_map<std::string, uint32_t> addresses;
  Json recipe = Json::object();
};
struct ResolvedPatch {
  uint32_t address = 0;
  Bytes original, replacement;
};
struct ConsumableSpec {
  std::string key;
  uint32_t base = 0;
  uint16_t idMin = 0;
  uint16_t idMax = 0;
  uint32_t storageCount = 0;
  uint32_t equipmentCount = 0;
  uint8_t width = 1;
  uint8_t stride = 1;
  bool baseDereference = false;
  std::vector<uint16_t> itemIds;
  std::vector<uint32_t> addresses;
  bool valid() const { return base != 0 && storageCount != 0 && !itemIds.empty(); }
};
enum class VariableStorage { Array, Bitset };
struct VariableLayout {
  VariableStorage storage = VariableStorage::Array;
  uint32_t base = 0;
  uint32_t stride = 4;
  uint32_t width = 4;
  uint32_t recordCount = 4096;
  uint32_t writableCount = 4096;
  uint32_t browseCount = 4096;
  bool isSigned = true;
  bool msb0 = true;
};
struct EngineProfile {
  EngineVersion version = EngineVersion::V64;
  WritePolicy writePolicy = WritePolicy::LegacyDirect;
  std::string displayVersion, variantName, identity;
  uint32_t imageBase = 0x400000, imageSize = 0;
  std::unordered_map<std::string, uint32_t> addresses, limits, remoteAddresses;
  std::vector<uint16_t> consumableItemIds;
  std::unordered_map<std::string, Capability> capabilities;
  Json metadata;
  ConsumableSpec consumables;
  VariableLayout boolVariables, intVariables, ptrVariables;
  bool is66() const { return version == EngineVersion::V66; }
  bool is65() const { return version == EngineVersion::V65; }
  bool canRead(const std::string &k) const {
    auto i = capabilities.find(k);
    return i != capabilities.end() && i->second.readable;
  }
  bool canWrite(const std::string &k) const {
    auto i = capabilities.find(k);
    return i != capabilities.end() && i->second.writable;
  }
  uint32_t address(const std::string &k, uint32_t fallback = 0) const {
    auto i = addresses.find(k);
    return i == addresses.end() ? fallback : i->second;
  }
  uint32_t remoteAddress(const std::string &k, uint32_t fallback = 0) const {
    auto i = remoteAddresses.find(k);
    return i == remoteAddresses.end() ? fallback : i->second;
  }
  uint32_t limit(const std::string &k, uint32_t fallback = 0) const {
    auto i = limits.find(k);
    return i == limits.end() ? fallback : i->second;
  }
};
class ProfileRegistry {
  Json catalog_;

public:
  ProfileRegistry();
  const Json &catalog() const { return catalog_; }
  EngineProfile legacy(int version) const;
  EngineProfile detect(const std::wstring &path, int versionHint = -1) const;
  void enrichLegacyDirect(EngineProfile &profile, const PEImage &image) const;
  std::vector<ResolvedPatch> resolvePatches(const PEImage &image,
                                            const std::string &key) const;
};
void applyItemFileLayout(EngineProfile &profile, const std::wstring &exePath);
} // namespace banshou
