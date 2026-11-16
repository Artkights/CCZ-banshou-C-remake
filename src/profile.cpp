#include "profile.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <sstream>

namespace banshou {
namespace {
Json objectAt(const Json &value, const char *key) {
  if (value.contains(key) && value[key].is_object())
    return value[key];
  return Json::object();
}

void copyUnsignedMap(const Json &source,
                     std::unordered_map<std::string, uint32_t> &target) {
  if (!source.is_object())
    return;
  for (auto it = source.begin(); it != source.end(); ++it) {
    if (it.value().is_number_unsigned() || it.value().is_number_integer())
      target[it.key()] = it.value().get<uint32_t>();
  }
}

ConsumableSpec parseConsumableSpec(const Json &spec) {
  ConsumableSpec out;
  if (!spec.is_object())
    return out;
  out.key = spec.value("key", std::string());
  out.base = spec.value("base", 0u);
  out.idMin = uint16_t(spec.value("id_min", 0u));
  out.storageCount = spec.value("storage_count", 0u);
  out.equipmentCount = spec.value("equipment_count", 0u);
  out.width = uint8_t(spec.value("width", 1u));
  out.stride = uint8_t(spec.value("stride", 1u));
  out.baseDereference = spec.value("base_dereference", false);
  if (spec.contains("ui_item_ids") && spec["ui_item_ids"].is_array())
    for (const auto &id : spec["ui_item_ids"])
      if (id.is_number_unsigned())
        out.itemIds.push_back(uint16_t(id.get<uint32_t>()));
  if (spec.contains("addresses") && spec["addresses"].is_array())
    for (const auto &address : spec["addresses"])
      if (address.is_number_unsigned())
        out.addresses.push_back(address.get<uint32_t>());
  out.idMax = out.itemIds.empty()
                  ? uint16_t(out.idMin + out.storageCount - 1)
                  : *std::max_element(out.itemIds.begin(), out.itemIds.end());
  return out;
}

void applyConsumableSpec(EngineProfile &profile, const ConsumableSpec &spec) {
  if (!spec.valid())
    return;
  profile.consumables = spec;
  profile.addresses["consumable_counts"] = spec.base;
  profile.limits["consumable_id_min"] = spec.idMin;
  profile.limits["consumable_ui_id_max"] = spec.idMax;
  profile.limits["consumable_storage_count"] = spec.storageCount;
  profile.limits["equipment_count"] = spec.equipmentCount;
  profile.consumableItemIds = spec.itemIds;
  profile.metadata["consumable_mapping_spec"] = {
      {"key", spec.key}, {"base", spec.base}, {"id_min", spec.idMin},
      {"id_max", spec.idMax}, {"storage_count", spec.storageCount},
      {"equipment_count", spec.equipmentCount}, {"width", spec.width},
      {"stride", spec.stride}, {"base_dereference", spec.baseDereference},
      {"ui_item_ids", spec.itemIds}, {"addresses", spec.addresses}};
}

VariableLayout parseVariableLayout(const EngineProfile &profile,
                                   const char *key,
                                   const char *addressKey,
                                   uint32_t fallbackCount,
                                   bool fallbackSigned) {
  VariableLayout out;
  out.base = profile.address(addressKey);
  out.recordCount = fallbackCount;
  out.writableCount = fallbackCount;
  out.browseCount = fallbackCount;
  out.isSigned = fallbackSigned;
  const auto layouts = objectAt(profile.metadata, "variable_layouts");
  if (!layouts.contains(key) || !layouts[key].is_object())
    return out;
  const auto &layout = layouts[key];
  const auto storage = layout.value("storage", std::string());
  out.storage = storage == "bitset" ? VariableStorage::Bitset
                                    : VariableStorage::Array;
  out.stride = layout.value("stride", out.storage == VariableStorage::Bitset
                                           ? 1u
                                           : 4u);
  out.width = layout.value("width", out.storage == VariableStorage::Bitset
                                          ? 1u
                                          : 4u);
  out.recordCount = layout.value("recordCount", fallbackCount);
  out.browseCount = layout.value("browseRecordCount", out.recordCount);
  out.isSigned = layout.value("signed", fallbackSigned);
  out.msb0 = layout.value("bitOrder", std::string("msb0")) != "lsb0";
  if (out.storage == VariableStorage::Bitset) {
    out.stride = 1;
    out.width = 1;
    if (out.browseCount > out.recordCount)
      out.recordCount = out.browseCount;
  }
  out.writableCount = out.recordCount;
  return out;
}

void parseVariableLayouts(EngineProfile &profile) {
  profile.boolVariables =
      parseVariableLayout(profile, "variables_bool", "bool_vars",
                          profile.is66() ? 1536u : 4096u, !profile.is66());
  profile.intVariables =
      parseVariableLayout(profile, "variables_int", "int_vars", 4096u, true);
  profile.ptrVariables =
      parseVariableLayout(profile, "variables_ptr", "ptr_vars", 481u, false);
  if (profile.boolVariables.base == 0)
    profile.boolVariables.base = 0x00492FC8;
  if (profile.intVariables.base == 0)
    profile.intVariables.base = 0x00502000;
  if (profile.ptrVariables.base == 0)
    profile.ptrVariables.base = 0x00506000;
}

struct PatternEntry {
  std::string name, pattern;
  int operandOffset = 0;
};

std::vector<PatternEntry> parsePatterns(const Json &node) {
  std::vector<PatternEntry> out;
  if (!node.is_object())
    return out;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it.value().is_array() || it.value().size() < 2)
      continue;
    PatternEntry entry;
    entry.name = it.key();
    entry.pattern = it.value()[0].get<std::string>();
    entry.operandOffset = it.value()[1].get<int>();
    out.push_back(std::move(entry));
  }
  return out;
}

bool locatePatternBase(const PEImage &image,
                       const std::vector<PatternEntry> &patterns,
                       uint32_t &base, std::string &reason) {
  if (patterns.empty()) {
    reason = "缺少签名配方";
    return false;
  }
  std::vector<uint32_t> bases;
  for (const auto &entry : patterns) {
    std::vector<uint32_t> hits;
    try {
      hits = image.findSignature(MaskedSignature::parse(entry.pattern), false);
    } catch (const std::exception &error) {
      reason = entry.name + "签名无效: " + error.what();
      return false;
    }
    if (hits.size() != 1) {
      reason = entry.name + "签名匹配数为" + std::to_string(hits.size());
      return false;
    }
    try {
      const auto operand = uint32_t(number(
          image.readVA(checkedAddress(hits.front(),
                                      uint64_t(entry.operandOffset), 4), 4),
          0, 4));
      bases.push_back(image.operandReference(operand));
    } catch (const std::exception &error) {
      reason = entry.name + "操作数读取失败: " + error.what();
      return false;
    }
  }
  if (std::adjacent_find(bases.begin(), bases.end(),
                         std::not_equal_to<uint32_t>()) != bases.end()) {
    reason = "读写/清零签名未指向同一基址";
    return false;
  }
  base = bases.front();
  if (base < 0x10000u || uint64_t(base) + 0x100u > 0x80000000ull) {
    reason = "消耗品基址不在32位用户地址域";
    return false;
  }
  return true;
}

ConsumableSpec makeSpec(const std::string &key, uint32_t base, uint16_t idMin,
                        uint16_t idMax, uint32_t storageCount,
                        uint32_t equipmentCount) {
  ConsumableSpec spec;
  spec.key = key;
  spec.base = base;
  spec.idMin = idMin;
  spec.idMax = idMax;
  spec.storageCount = storageCount;
  spec.equipmentCount = equipmentCount;
  for (uint32_t id = idMin; id <= idMax; ++id)
    spec.itemIds.push_back(uint16_t(id));
  return spec;
}

Capability disabledCapability(const std::string &reason,
                             const std::string &reasonCode) {
  Capability capability;
  capability.state = "Unavailable";
  capability.reason = reason;
  capability.reasonCode = reasonCode;
  return capability;
}

uint32_t catalogLimit(const Json &catalog, const char *key, uint32_t fallback) {
  if (catalog.contains("limits") && catalog["limits"].contains(key) &&
      catalog["limits"][key].is_number_unsigned())
    return catalog["limits"][key].get<uint32_t>();
  return fallback;
}

uint32_t catalogAddress(const Json &catalog, const char *key,
                        uint32_t fallback) {
  if (catalog.contains("addresses") && catalog["addresses"].contains(key) &&
      catalog["addresses"][key].is_number_unsigned())
    return catalog["addresses"][key].get<uint32_t>();
  return fallback;
}
} // namespace

ProfileRegistry::ProfileRegistry() {
  try {
    catalog_ = resourceJson(103);
  } catch (...) {
    catalog_ = Json::object();
  }
}

EngineProfile ProfileRegistry::legacy(int version) const {
  if (version < 0 || version > 6)
    throw Error("Unknown engine profile");
  EngineProfile profile;
  profile.version = static_cast<EngineVersion>(version);
  Json j;
  if (version == 6) {
    if (!catalog_.contains("base66"))
      throw Error("6.6画像目录缺失");
    const auto base = catalog_["base66"];
    j["display_version"] = "6.6";
    j["variant_name"] = "6.6 组合画像";
    j["preferred_image_base"] =
        catalogAddress(base, "image_base", 0x400000);
    j["image_size"] = 0;
    j["addresses"] = base["addresses"];
    j["limits"] = base["limits"];
    j["metadata"] = base["metadata"];
  } else {
    const auto key = std::to_string(version);
    if (!catalog_.contains("legacy") || !catalog_["legacy"].contains(key))
      throw Error("Unknown engine profile");
    j = catalog_["legacy"][key];
  }
  profile.displayVersion = j.value("display_version", std::to_string(version));
  profile.variantName = j.value("variant_name", "");
  profile.imageBase = j.value("preferred_image_base", 0x400000u);
  profile.imageSize = j.value("image_size", 0u);
  copyUnsignedMap(objectAt(j, "addresses"), profile.addresses);
  copyUnsignedMap(objectAt(j, "limits"), profile.limits);
  profile.metadata = objectAt(j, "metadata");
  if (j.contains("capabilities") && j["capabilities"].is_object()) {
    for (auto it = j["capabilities"].begin(); it != j["capabilities"].end();
         ++it) {
      if (!it.value().is_object())
        continue;
      Capability capability;
      capability.state = it.value().value("state", std::string());
      capability.readable = capability.state != "Unavailable";
      capability.writable = capability.state == "Enabled";
      capability.reason = it.value().value("reason", std::string());
      capability.reasonCode = it.value().value("reason_code", std::string());
      capability.recipeId = it.value().value("recipe_id", std::string());
      if (it.value().contains("evidence_ids") &&
          it.value()["evidence_ids"].is_array())
        for (const auto &id : it.value()["evidence_ids"])
          if (id.is_string())
            capability.evidenceIds.push_back(id.get<std::string>());
      if (it.value().contains("addresses") &&
          it.value()["addresses"].is_object())
        copyUnsignedMap(it.value()["addresses"], capability.addresses);
      if (it.value().contains("recipe") && it.value()["recipe"].is_object())
        capability.recipe = it.value()["recipe"];
      profile.capabilities[it.key()] = capability;
    }
  }
  if (profile.capabilities.empty()) {
    for (const auto *key :
         {"person", "battle", "warehouse", "item", "effect",
          "effect_exclusive", "power", "variables", "variables_bool",
          "variables_int", "variables_ptr", "diy", "remote", "recalculate",
          "revive", "weather", "turn_refresh_direction",
          "turn_refresh_position", "control_friendly",
          "control_all_factions", "control_no_ai",
          "control_move_after_wait", "control_move_255",
          "control_pass_through", "control_auto_return"}) {
      Capability capability;
      capability.readable = true;
      capability.writable = true;
      capability.state = "Enabled";
      capability.reason = "legacy direct";
      profile.capabilities[key] = capability;
    }
  }

  if (profile.metadata.contains("consumable_mapping_spec")) {
    applyConsumableSpec(
        profile, parseConsumableSpec(profile.metadata["consumable_mapping_spec"]));
  } else if (profile.metadata.contains("consumable_mapping") &&
             profile.metadata["consumable_mapping"].is_object()) {
    ConsumableSpec spec;
    const auto &mapping = profile.metadata["consumable_mapping"];
    spec.key = mapping.value("recipeKey", std::string());
    spec.base = profile.address("consumable_counts");
    spec.idMin = uint16_t(mapping.value("idMin", 0u));
    spec.idMax = uint16_t(mapping.value("idMax", 0u));
    spec.storageCount = mapping.value("storageCount", 0u);
    spec.equipmentCount = mapping.value("equipmentCount", 0u);
    spec.width = uint8_t(mapping.value("width", 1u));
    spec.stride = uint8_t(mapping.value("stride", 1u));
    if (mapping.contains("itemIds") && mapping["itemIds"].is_array())
      for (const auto &id : mapping["itemIds"])
        if (id.is_number_unsigned())
          spec.itemIds.push_back(uint16_t(id.get<uint32_t>()));
    if (mapping.contains("addresses") && mapping["addresses"].is_array())
      for (const auto &address : mapping["addresses"])
        if (address.is_number_unsigned())
          spec.addresses.push_back(address.get<uint32_t>());
    applyConsumableSpec(profile, spec);
  }
  if (profile.consumableItemIds.empty() &&
      profile.metadata.contains("consumable_item_ids") &&
      profile.metadata["consumable_item_ids"].is_array()) {
    for (const auto &id : profile.metadata["consumable_item_ids"])
      if (id.is_number_unsigned() && id.get<uint32_t>() <= 0xffff)
        profile.consumableItemIds.push_back(uint16_t(id.get<uint32_t>()));
  }
  if (version < 6 && j.contains("remote_calls") &&
      j["remote_calls"].is_object()) {
    for (auto it = j["remote_calls"].begin(); it != j["remote_calls"].end();
         ++it) {
      if (it.value().is_object() &&
          it.value().contains("address") &&
          it.value()["address"].is_number_unsigned())
        profile.remoteAddresses[it.key()] =
            it.value()["address"].get<uint32_t>();
    }
  }
  profile.remoteAddresses["recalculate"] = 0x004075DF;
  profile.remoteAddresses["weather"] = 0x0041D9D1;
  profile.remoteAddresses["turn_refresh_direction"] =
      version == 6 ? 0x004575B8 : 0x00457428;
  profile.remoteAddresses["turn_refresh_position"] = 0x004594DD;
  if (version == 6)
    profile.remoteAddresses["revive"] = 0x004092C0;
  else if (version == 0 || version == 5)
    profile.remoteAddresses["revive"] = 0x004092C7;
  else if (version == 4)
    profile.remoteAddresses["revive"] = 0x0040930F;
  else
    profile.remoteAddresses["revive"] = 0x004092E0;
  if (catalog_.contains("remoteCalls") &&
      catalog_["remoteCalls"].contains(std::to_string(version)) &&
      catalog_["remoteCalls"][std::to_string(version)].is_object()) {
    for (auto it = catalog_["remoteCalls"][std::to_string(version)].begin();
         it != catalog_["remoteCalls"][std::to_string(version)].end(); ++it) {
      if (!it.value().is_object() || !it.value().contains("address") ||
          !it.value()["address"].is_number_unsigned())
        continue;
      const auto address = it.value()["address"].get<uint32_t>();
      if (address)
        profile.remoteAddresses[it.key()] = address;
    }
  }

  parseVariableLayouts(profile);
  return profile;
}

void ProfileRegistry::enrichLegacyDirect(EngineProfile &profile,
                                         const PEImage &image) const {
  parseVariableLayouts(profile);
  if (profile.version == EngineVersion::V65 ||
      profile.version == EngineVersion::V66) {
    profile.addresses["effect_names"] = 0x004CC600;
    profile.limits["effect_name_stride"] = 16;
    profile.limits["effect_name_size"] = 15;
  }
  auto itemHits = image.findSignature(
      MaskedSignature::parse("55 8B EC 6B C9 19 81 C1 ?? ?? ?? ?? 5D C3"),
      true);
  if (itemHits.size() == 1) {
    try {
      auto operand = uint32_t(
          number(image.readVA(checkedAddress(itemHits.front(), 8, 4), 4), 0, 4));
      profile.addresses["item_table"] = image.operandReference(operand);
      profile.limits["item_stride"] = 25;
      profile.limits["item_name_size"] =
          profile.version == EngineVersion::V66 ? 15 : 17;
    } catch (...) {
    }
  }

  const bool standard66 = profile.version == EngineVersion::V66;
  const bool expanded = profile.version == EngineVersion::V64 ||
                        profile.version == EngineVersion::V65;
  if (standard66) {
    profile.limits["item_count"] = 255;
    profile.limits["item_record_count"] = 256;
    profile.limits["equipment_count"] = 160;
    profile.limits["item_name_size"] = 15;
    profile.limits["item_extension_active"] = 0;
    ConsumableSpec spec;
    std::string reason;
    std::vector<ConsumableSpec> candidates;
    const auto &catalog = catalog_;
    if (catalog.contains("consumables66")) {
      uint32_t base = 0;
      if (locatePatternBase(image,
                            parsePatterns(catalog["consumables66"]["patterns"]),
                            base, reason))
        candidates.push_back(makeSpec("standard-66-352-v1", base, 160, 511,
                                     352, 160));
    }
    if (catalog.contains("consumablesLegacy")) {
      uint32_t base = 0;
      if (locatePatternBase(
              image, parsePatterns(catalog["consumablesLegacy"]["patterns"]),
              base, reason))
        candidates.push_back(makeSpec("legacy-expanded-105-v1", base, 150, 254,
                                     105, 150));
    }
    if (catalog.contains("consumablesSingleExe")) {
      uint32_t base = 0;
      if (locatePatternBase(
              image, parsePatterns(catalog["consumablesSingleExe"]["patterns"]),
              base, reason))
        candidates.push_back(makeSpec("single-exe-66-256-v1", base, 256, 511,
                                     256, 160));
    }
    if (candidates.size() == 1) {
      applyConsumableSpec(profile, candidates.front());
    } else if (candidates.size() > 1) {
      profile.consumables = ConsumableSpec{};
      profile.metadata["consumable_error"] = "多个消耗品配方同时匹配，拒绝猜测";
      profile.capabilities["item_consumables"] = disabledCapability(
          "多个消耗品配方同时匹配", "CONSUMABLE_AMBIGUOUS");
    } else {
      profile.consumables = ConsumableSpec{};
      profile.metadata["consumable_error"] = reason;
      profile.capabilities["item_consumables"] =
          disabledCapability(reason, "CONSUMABLE_UNRESOLVED");
    }
    // 6.6 has two independently proven BOOL layouts: the packed MSB-first
    // bitmap and the DWORD save/load loop.  Prefer the packed layout, then
    // fall back to the DWORD count; an ambiguous or missing pair stays
    // read-only, matching Python's formatVerified=false behavior.
    const auto patterns = objectAt(catalog_, "patterns");
    const auto uniqueHits = [&](const char *key) {
      if (!patterns.contains(key))
        return std::vector<uint32_t>{};
      try {
        return image.findSignature(
            MaskedSignature::parse(patterns[key].get<std::string>()), true);
      } catch (...) {
        return std::vector<uint32_t>{};
      }
    };
    const auto packedReader = uniqueHits("BOOL_READER_PATTERN_66");
    const auto packedWriter = uniqueHits("BOOL_WRITER_PATTERN_66");
    const bool packedVerified =
        packedReader.size() == 1 && packedWriter.size() == 1;
    uint32_t dwordCount = 0;
    std::array<size_t, 5> boolHits{packedReader.size(), packedWriter.size(), 0,
                                   0, 0};
    if (!packedVerified) {
      const auto dwordReader = uniqueHits("BOOL_DWORD_READER_PATTERN");
      const auto dwordWriter = uniqueHits("BOOL_DWORD_WRITER_PATTERN");
      const auto saveLoop = uniqueHits("BOOL_DWORD_SAVE_LOOP_PATTERN");
      boolHits[2] = dwordReader.size();
      boolHits[3] = dwordWriter.size();
      boolHits[4] = saveLoop.size();
      if (dwordReader.size() == 1 && dwordWriter.size() == 1 &&
          saveLoop.size() == 1) {
        try {
          const auto site = saveLoop.front();
          const auto count =
              uint32_t(number(image.readVA(checkedAddress(site, 5, 2), 2), 0, 2));
          const auto following = uint32_t(
              number(image.readVA(checkedAddress(site, 41, 4), 4), 0, 4));
          if (count >= 1 && count <= 0x7FFF &&
              following == 0x38u + count * 4u)
            dwordCount = count;
        } catch (...) {
        }
      }
    }
    if (packedVerified) {
      profile.boolVariables.storage = VariableStorage::Bitset;
      profile.boolVariables.stride = 1;
      profile.boolVariables.width = 1;
      profile.boolVariables.recordCount = 2048;
      profile.boolVariables.writableCount = 2048;
      profile.boolVariables.browseCount = 2048;
      profile.boolVariables.isSigned = false;
      profile.boolVariables.msb0 = true;
      profile.limits["variable_bool_count"] = 2048;
      profile.limits["variable_bool_browse_count"] = 2048;
      profile.limits["variable_bool_writable_count"] = 2048;
    } else if (dwordCount) {
      profile.boolVariables.storage = VariableStorage::Array;
      profile.boolVariables.stride = 4;
      profile.boolVariables.width = 4;
      profile.boolVariables.recordCount = dwordCount;
      profile.boolVariables.writableCount = dwordCount;
      profile.boolVariables.browseCount = dwordCount;
      profile.boolVariables.isSigned = false;
      profile.limits["variable_bool_count"] = dwordCount;
      profile.limits["variable_bool_browse_count"] = dwordCount;
      profile.limits["variable_bool_writable_count"] = dwordCount;
    } else {
      profile.boolVariables.storage = VariableStorage::Bitset;
      profile.boolVariables.stride = 1;
      profile.boolVariables.width = 1;
      profile.boolVariables.recordCount = 2048;
      profile.boolVariables.writableCount = 2048;
      profile.boolVariables.browseCount = 2048;
      profile.boolVariables.isSigned = false;
      profile.boolVariables.msb0 = true;
      profile.limits["variable_bool_count"] = 2048;
      profile.limits["variable_bool_browse_count"] = 2048;
      profile.limits["variable_bool_writable_count"] = 2048;
    }
    profile.boolVariables.base = profile.address("bool_vars", 0x00492FC8);
    profile.metadata["boolean_diagnostics"] = {
        {"packedReaderHits", boolHits[0]},
        {"packedWriterHits", boolHits[1]},
        {"dwordReaderHits", boolHits[2]},
        {"dwordWriterHits", boolHits[3]},
        {"dwordSaveLoopHits", boolHits[4]},
        {"dwordCount", dwordCount},
    };
  } else if (expanded) {
    std::string reason;
    uint32_t base = 0;
    if (catalog_.contains("consumablesLegacy") &&
        locatePatternBase(
            image, parsePatterns(catalog_["consumablesLegacy"]["patterns"]),
            base, reason)) {
      applyConsumableSpec(
          profile, makeSpec("legacy-expanded-105-v1", base, 150, 254, 105,
                            150));
      profile.metadata["consumable_signature_verified"] = true;
    } else if (profile.consumables.valid()) {
      profile.metadata["consumable_error"] = reason;
      profile.metadata["consumable_signature_verified"] = false;
    }
    profile.limits["item_count"] = 255;
    profile.limits["item_record_count"] = 256;
    profile.limits["item_name_size"] = 17;
  } else {
    if (catalog_.contains("consumablesSplit43")) {
      const auto &split = catalog_["consumablesSplit43"];
      ConsumableSpec spec;
      spec.key = split.value("key", "legacy-split-43-v1");
      spec.base = split.value("base", 0u);
      spec.idMin = uint16_t(split.value("id_min", 87u));
      spec.storageCount = split.value("storage_count", 43u);
      spec.equipmentCount = split.value("equipment_count", 154u);
      spec.idMax = uint16_t(spec.idMin + spec.storageCount - 1);
      if (split.contains("ui_item_ids"))
        for (const auto &id : split["ui_item_ids"])
          if (id.is_number_unsigned())
            spec.itemIds.push_back(uint16_t(id.get<uint32_t>()));
      if (split.contains("addresses"))
        for (const auto &address : split["addresses"])
          if (address.is_number_unsigned())
            spec.addresses.push_back(address.get<uint32_t>());
      applyConsumableSpec(profile, spec);
    }
  }
}

void applyItemFileLayout(EngineProfile &profile, const std::wstring &exePath) {
  if (profile.version == EngineVersion::V61 ||
      profile.version == EngineVersion::V62 ||
      profile.version == EngineVersion::V63 ||
      profile.version == EngineVersion::V63MP)
    return;
  constexpr size_t kRowSize = 25;
  constexpr size_t kLowEnd = 33164 + 104 * kRowSize;
  constexpr size_t kHighEnd = 152 * kRowSize;
  Bytes dataFile, starFile;
  const auto root = parentPath(exePath);
  try {
    dataFile = readFile(root + L"\\Data.e5");
  } catch (...) {
  }
  try {
    starFile = readFile(root + L"\\Star.e5");
  } catch (...) {
  }
  if (dataFile.size() < kLowEnd || starFile.size() < kHighEnd) {
    if (profile.is66()) {
      profile.limits["item_count"] = 512;
      profile.limits["item_record_count"] = 512;
      profile.limits["item_extension_active"] = 1;
    } else {
      profile.limits["item_count"] = 255;
      profile.limits["item_record_count"] = 256;
      profile.limits["item_extension_active"] = 0;
    }
    return;
  }
  if (!profile.is66()) {
    profile.limits["item_count"] = 255;
    profile.limits["item_record_count"] = 256;
    profile.limits["item_extension_active"] = 0;
    return;
  }
  if (profile.consumables.valid() &&
      profile.consumables.key == "single-exe-66-256-v1") {
    profile.limits["item_count"] = 512;
    profile.limits["item_record_count"] = 512;
    profile.limits["item_extension_active"] = 1;
    return;
  }
  constexpr size_t kStarCapacity66 = 0x27D8;
  constexpr size_t kSentinelOffset = 151 * kRowSize;
  bool real255 = false;
  if (starFile.size() >= kSentinelOffset + 15) {
    std::string raw(reinterpret_cast<const char *>(starFile.data() +
                                                   kSentinelOffset),
                    15);
    raw = raw.substr(0, raw.find('\0'));
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\r' ||
                            raw.back() == '\n'))
      raw.pop_back();
    std::string upper;
    for (char c : raw)
      upper.push_back(char(std::toupper(static_cast<unsigned char>(c))));
    real255 = !raw.empty() && upper != "FF-END" && upper != "END";
  }
  if (starFile.size() >= kStarCapacity66) {
    profile.limits["item_count"] = 512;
    profile.limits["item_record_count"] = 512;
    profile.limits["item_extension_active"] = 1;
  } else if (real255) {
    profile.limits["item_count"] = 256;
    profile.limits["item_record_count"] = 256;
    profile.limits["item_extension_active"] = 0;
  } else {
    profile.limits["item_count"] = 255;
    profile.limits["item_record_count"] = 256;
    profile.limits["item_extension_active"] = 0;
  }
}

EngineProfile ProfileRegistry::detect(const std::wstring &path,
                                      int versionHint) const {
  PEImage image(path);
  if (!image.isX86())
    throw Error("Target is not x86");
  auto digest = image.sha256();
  if (catalog_.contains("knownLegacy") &&
      catalog_["knownLegacy"].contains(digest)) {
    const auto &known = catalog_["knownLegacy"][digest];
    const int version = known.value("version", 0);
    auto p = legacy(version);
    p.variantName = known.value("variant", p.variantName);
    p.identity = digest;
    p.imageBase = image.imageBase();
    p.imageSize = image.imageSize();
    enrichLegacyDirect(p, image);
    applyItemFileLayout(p, path);
    return p;
  }
  if (versionHint >= 0) {
    auto p = legacy(versionHint);
    p.identity = digest;
    p.imageSize = image.imageSize();
    enrichLegacyDirect(p, image);
    applyItemFileLayout(p, path);
    return p;
  }
  if (catalog_.contains("known66") && catalog_["known66"].contains(digest)) {
    auto p = legacy(6);
    const auto &known = catalog_["known66"][digest];
    if (known.is_array() && !known.empty())
      p.variantName = known[0].get<std::string>();
    p.identity = digest;
    p.imageSize = image.imageSize();
    enrichLegacyDirect(p, image);
    applyItemFileLayout(p, path);
    return p;
  }
  int resourceVersion = -1;
  DWORD ignored = 0;
  auto bytes = GetFileVersionInfoSizeW(path.c_str(), &ignored);
  if (bytes) {
    Bytes data(bytes);
    if (GetFileVersionInfoW(path.c_str(), 0, bytes, data.data())) {
      VS_FIXEDFILEINFO *info = nullptr;
      UINT len = 0;
      if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info),
                         &len) &&
          info && len >= sizeof(VS_FIXEDFILEINFO)) {
        auto major = HIWORD(info->dwFileVersionMS);
        auto minor = LOWORD(info->dwFileVersionMS);
        if (major == 6 && minor == 6)
          resourceVersion = 6;
        else if (major == 6 && minor == 5)
          resourceVersion = 5;
        else if (major == 6 && minor == 4)
          resourceVersion = 0;
        else if (major == 6 && minor == 3)
          resourceVersion = 2;
        else if (major == 6 && minor == 2)
          resourceVersion = 3;
        else if (major == 6 && minor == 1)
          resourceVersion = 4;
      }
    }
  }
  if (resourceVersion >= 0) {
    auto p = legacy(resourceVersion);
    p.identity = digest;
    p.imageSize = image.imageSize();
    p.variantName = "版本资源画像，待运行时签名确认";
    enrichLegacyDirect(p, image);
    applyItemFileLayout(p, path);
    return p;
  }
  // The historic executables usually carry a 1.0.0.x resource instead of a
  // 6.x version.  Confirm the 6.4/6.5 consumable ABI by its four signatures
  // before falling back to the 6.6 base profile.
  {
    auto legacy65 = legacy(5);
    legacy65.identity = digest;
    legacy65.imageBase = image.imageBase();
    legacy65.imageSize = image.imageSize();
    legacy65.variantName = "旧版签名画像，待运行时功能确认";
    enrichLegacyDirect(legacy65, image);
    if (legacy65.metadata.value("consumable_signature_verified", false)) {
      applyItemFileLayout(legacy65, path);
      return legacy65;
    }
  }
  EngineProfile unknown = legacy(6);
  unknown.displayVersion = "未知 x86";
  unknown.variantName = "按 6.6 基础画像直写；可在附加前手动选择版本";
  unknown.identity = digest;
  unknown.imageBase = image.imageBase();
  unknown.imageSize = image.imageSize();
  enrichLegacyDirect(unknown, image);
  applyItemFileLayout(unknown, path);
  return unknown;
}

std::vector<ResolvedPatch>
ProfileRegistry::resolvePatches(const PEImage &image,
                                const std::string &key) const {
  std::vector<ResolvedPatch> out;
  if (!catalog_.contains("patchLocators") ||
      !catalog_["patchLocators"].contains(key))
    throw Error("补丁配方不存在");
  for (auto &item : catalog_["patchLocators"][key]) {
    auto pattern = item.at(0).get<std::string>();
    auto hits = image.findSignature(MaskedSignature::parse(pattern), true);
    if (hits.empty())
      continue;
    auto address = checkedAddress(hits.front(), item.at(1).get<int>());
    auto original = unhex(item.at(2).at("hex").get<std::string>());
    auto replacement = unhex(item.at(3).at("hex").get<std::string>());
    out.push_back({address, std::move(original), std::move(replacement)});
  }
  if (out.empty())
    throw Error("补丁位置未找到");
  return out;
}
} // namespace banshou
