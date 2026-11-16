#include "evidence.hpp"
#include "image.hpp"
#include "profile.hpp"
#include <capstone/capstone.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <tlhelp32.h>
#include <utility>

namespace banshou {
namespace {

constexpr const char *kCollector = "wrench-evidence-collector/1";

std::string lowerAscii(std::string value) {
  for (auto &c : value)
    if (c >= 'A' && c <= 'Z')
      c = char(c - 'A' + 'a');
  return value;
}

std::string trimAscii(const std::string &value) {
  size_t begin = 0, end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
    ++begin;
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])))
    --end;
  return value.substr(begin, end - begin);
}

std::string hexAddress(uint32_t value) {
  char buffer[16]{};
  std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
  return buffer;
}

bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

bool isWordChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') || c == '_';
}

std::string normalizeInstruction(const std::string &mnemonic,
                                 const std::string &operands) {
  std::string text;
  text.reserve(operands.size());
  for (size_t i = 0; i < operands.size();) {
    if (operands[i] == '0' && i + 2 <= operands.size() &&
        (operands[i + 1] == 'x' || operands[i + 1] == 'X') &&
        i + 2 < operands.size() && isHexDigit(operands[i + 2])) {
      i += 2;
      while (i < operands.size() && isHexDigit(operands[i]))
        ++i;
      text += "<imm>";
      continue;
    }
    if (operands[i] >= '0' && operands[i] <= '9') {
      const bool leftBoundary =
          i == 0 || !isWordChar(operands[i - 1]);
      if (!leftBoundary) {
        text += operands[i++];
        continue;
      }
      size_t end = i;
      while (end < operands.size() && operands[end] >= '0' &&
             operands[end] <= '9')
        ++end;
      const bool rightBoundary =
          end == operands.size() || !isWordChar(operands[end]);
      if (rightBoundary) {
        text += "<imm>";
        i = end;
      } else {
        text.append(operands, i, end - i);
        i = end;
      }
      continue;
    }
    text += operands[i++];
  }
  std::string compressed;
  bool space = false;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      space = true;
      continue;
    }
    if (space && !compressed.empty())
      compressed += ' ';
    space = false;
    compressed += c;
  }
  const auto normalized = trimAscii(compressed);
  auto mnemonicText = lowerAscii(mnemonic);
  if (normalized.empty())
    return mnemonicText;
  return mnemonicText + " " + lowerAscii(normalized);
}

Json disassembleRaw(const Bytes &raw, uint32_t start) {
  csh handle = 0;
  if (cs_open(CS_ARCH_X86, CS_MODE_32, &handle) != CS_ERR_OK)
    throw Error("Capstone 初始化失败");
  cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);
  cs_insn *instructions = nullptr;
  auto count = cs_disasm(handle, raw.data(), raw.size(), start, 0, &instructions);
  Json list = Json::array();
  Json normalized = Json::array();
  std::string fingerprintText;
  for (size_t i = 0; i < count; ++i) {
    const auto &instruction = instructions[i];
    Bytes bytes(instruction.bytes, instruction.bytes + instruction.size);
    Json item;
    item["address"] = hexAddress(uint32_t(instruction.address));
    item["bytesHex"] = hex(bytes);
    item["mnemonic"] = instruction.mnemonic;
    item["operands"] = instruction.op_str;
    list.push_back(item);
    auto normalizedText =
        normalizeInstruction(instruction.mnemonic, instruction.op_str);
    normalized.push_back(normalizedText);
    if (!fingerprintText.empty())
      fingerprintText += "\n";
    fingerprintText += normalizedText;
  }
  if (instructions)
    cs_free(instructions, count);
  cs_close(&handle);
  Json result;
  const auto digest = sha256(
      Bytes(fingerprintText.begin(), fingerprintText.end()));
  result["instructions"] = list;
  result["normalizedInstructions"] = normalized;
  result["normalizedFingerprint"] = digest;
  return result;
}

Json disassemble(const PEImage &image, uint32_t address, uint32_t before = 16,
                 uint32_t size = 64) {
  const auto start = std::max(image.imageBase(), address - before);
  Json result;
  Bytes raw;
  try {
    raw = image.readVA(start, size);
  } catch (const std::exception &error) {
    result["start"] = hexAddress(start);
    result["target"] = hexAddress(address);
    result["fileBacked"] = false;
    result["reason"] = error.what();
    result["instructions"] = Json::array();
    result["normalizedInstructions"] = Json::array();
    result["normalizedFingerprint"] = "";
    return result;
  }
  result = disassembleRaw(raw, start);
  result["start"] = hexAddress(start);
  result["target"] = hexAddress(address);
  result["fileBacked"] = true;
  result["bytesHex"] = hex(raw);
  return result;
}

void overlayU32(Bytes &buffer, size_t offset, uint32_t value,
                size_t width = 4) {
  for (size_t i = 0; i < width; ++i)
    buffer[offset + i] = uint8_t((value >> (i * 8)) & 0xff);
}

Json readJson(const std::wstring &path) {
  const auto bytes = readFile(path);
  return Json::parse(std::string(bytes.begin(), bytes.end()));
}

void writePrettyJson(const std::wstring &path, const Json &value) {
  writeText(path, value.dump(2) + "\n");
}

uint64_t fileLength(const std::wstring &path) {
  std::error_code error;
  auto size = std::filesystem::file_size(std::filesystem::path(path), error);
  if (error)
    throw Error("无法读取目标文件长度: " + error.message());
  return uint64_t(size);
}

std::string safeFeatureName(const std::string &feature) {
  std::string out;
  out.reserve(feature.size());
  for (char c : feature) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                         c == '-';
    out += allowed ? c : '_';
  }
  return out.empty() ? "feature" : out;
}

std::vector<std::string> requiredRuntimeChecks(
    const std::string &featureKey) {
  static const std::map<std::string, std::vector<std::string>> table = {
      {"control_all_factions",
       {"signature_unique", "instruction_boundaries", "oracle_match",
        "camp_matrix_complete", "rollback_verified", "three_toggles",
        "external_change_refused"}},
      {"control_auto_return",
       {"signature_unique", "native_call_targets", "three_patch_sites",
        "stationary_wait_verified", "moved_wait_verified",
        "reselect_verified", "menu_verified", "safe_fields_unchanged",
        "battle_switch_verified", "rollback_verified", "three_toggles",
        "external_change_refused"}},
      {"turn_refresh_direction",
       {"signature_unique", "abi_verified", "camp_matrix_complete",
        "direction_only", "rollback_verified"}},
      {"turn_refresh_position",
       {"signature_unique", "abi_verified", "camp_matrix_complete",
        "map_occupancy", "collision", "pathfinding", "selection_refresh",
        "render_refresh", "rollback_verified"}},
      {"item_consumables",
       {"static_formula", "boundary_150", "middle_id", "boundary_254",
        "category_matrix", "negative_controls", "write_255_restore",
        "rollback_verified"}},
  };
  auto it = table.find(featureKey);
  if (it != table.end())
    return it->second;
  return {"runtime_probe", "behavior_verified"};
}

Json makeBundle(const std::string &featureKey, const std::string &targetSha,
                uint64_t fileSize, const std::string &recipeIdValue,
                const Json &recipe, const std::string &phase,
                const Json &staticEvidence, const Json &samples,
                const Json &verification) {
  Json bundle;
  bundle["schemaVersion"] = kEvidenceSchemaVersion;
  bundle["featureKey"] = featureKey;
  bundle["target"] = {{"sha256", targetSha}, {"fileSize", fileSize}};
  bundle["recipeId"] = recipeIdValue;
  bundle["recipe"] = recipe;
  bundle["phase"] = phase;
  bundle["staticEvidence"] = staticEvidence;
  bundle["runtimeSamples"] = samples;
  bundle["verification"] = verification;
  return evidenceFinalizeBundle(bundle);
}

Json defaultVerification() {
  Json checks = Json::object();
  Json verification;
  verification["collector"] = kCollector;
  verification["status"] = "captured";
  verification["automated"] = true;
  verification["checks"] = checks;
  return verification;
}

bool lookupPath(const Json &sample, const std::string &path, Json &out) {
  const Json *current = &sample;
  std::stringstream stream(path);
  std::string part;
  while (std::getline(stream, part, '.')) {
    if (!current->is_object() || !current->contains(part))
      return false;
    current = &(*current)[part];
  }
  out = *current;
  return true;
}

bool isEmptyValue(const Json &value) {
  return value.is_null() || (value.is_string() && value.get<std::string>().empty()) ||
         (value.is_array() && value.empty()) ||
         (value.is_object() && value.empty());
}

bool evaluatePredicate(const Json &predicate, const Json &samples) {
  if (!predicate.is_object())
    return false;
  const auto kind = predicate.value("type", std::string());
  if (kind == "min-samples") {
    const auto minimum = predicate.value("minimum", 1);
    return samples.is_array() && int(samples.size()) >= minimum;
  }
  if (kind == "tag-values") {
    const auto tag = predicate.value("tag", std::string());
    const auto minimum = predicate.value("minimumPerValue", 1);
    if (!samples.is_array() || !predicate.contains("values") ||
        !predicate["values"].is_array() || predicate["values"].empty())
      return false;
    std::map<std::string, int> counts;
    for (const auto &item : predicate["values"])
      counts[item.get<std::string>()] = 0;
    for (const auto &sample : samples) {
      if (!sample.is_object() || !sample.contains("tags"))
        continue;
      const auto &tags = sample["tags"];
      if (!tags.is_object() || !tags.contains(tag))
        continue;
      auto value = tags[tag].is_string() ? tags[tag].get<std::string>()
                                         : tags[tag].dump();
      auto it = counts.find(value);
      if (it != counts.end())
        ++it->second;
    }
    for (const auto &entry : counts)
      if (entry.second < minimum)
        return false;
    return true;
  }
  if (kind == "field-present") {
    const auto path = predicate.value("path", std::string());
    if (!samples.is_array() || samples.empty())
      return false;
    for (const auto &sample : samples) {
      Json value;
      if (!lookupPath(sample, path, value) || isEmptyValue(value))
        return false;
    }
    return true;
  }
  if (kind == "all-equal") {
    const auto path = predicate.value("path", std::string());
    if (!samples.is_array() || samples.empty())
      return false;
    for (const auto &sample : samples) {
      Json value;
      if (!lookupPath(sample, path, value))
        return false;
      if (value != predicate.value("value", Json()))
        return false;
    }
    return true;
  }
  if (kind == "distinct-count") {
    const auto path = predicate.value("path", std::string());
    const auto minimum = predicate.value("minimum", 1);
    if (!samples.is_array())
      return false;
    std::set<std::string> distinct;
    for (const auto &sample : samples) {
      Json value;
      if (!lookupPath(sample, path, value))
        return false;
      distinct.insert(value.dump());
    }
    return int(distinct.size()) >= minimum;
  }
  if (kind == "watch-relation") {
    const auto tag = predicate.value("tag", std::string("phase"));
    const auto watch = predicate.value("watch", std::string());
    const auto leftTag = predicate.value("left", std::string());
    const auto rightTag = predicate.value("right", std::string());
    const auto relation = predicate.value("relation", std::string());
    std::set<std::string> left, right;
    const auto collect = [&](const std::string &expected,
                             std::set<std::string> &target) {
      for (const auto &sample : samples) {
        if (!sample.is_object() || !sample.contains("tags") ||
            !sample["tags"].is_object() || !sample.contains("watches") ||
            !sample["watches"].is_object())
          continue;
        auto tagValue = sample["tags"].contains(tag) &&
                                sample["tags"][tag].is_string()
                            ? sample["tags"][tag].get<std::string>()
                            : std::string();
        if (tagValue != expected)
          continue;
        if (!sample["watches"].contains(watch))
          continue;
        const auto &entry = sample["watches"][watch];
        if (!entry.is_object() || !entry.contains("bytesHex") ||
            !entry["bytesHex"].is_string())
          continue;
        auto text = entry["bytesHex"].get<std::string>();
        if (!text.empty())
          target.insert(text);
      }
    };
    collect(leftTag, left);
    collect(rightTag, right);
    if (left.empty() || right.empty())
      return false;
    if (relation == "equal")
      return left == right;
    if (relation == "different") {
      for (const auto &value : left)
        if (right.count(value))
          return false;
      return true;
    }
    return false;
  }
  if (kind == "all-of") {
    if (!predicate.contains("predicates") ||
        !predicate["predicates"].is_array() ||
        predicate["predicates"].empty())
      return false;
    for (const auto &child : predicate["predicates"])
      if (!evaluatePredicate(child, samples))
        return false;
    return true;
  }
  return false;
}

std::string recipeKeyForFeature(const std::string &featureKey,
                                const EngineProfile &profile) {
  (void)profile;
  return featureKey;
}

Json loadRecipe(const std::wstring &path, const std::string &featureKey,
                const EngineProfile &profile) {
  if (!path.empty()) {
    auto raw = readJson(path);
    if (raw.is_object() && raw.contains("recipe"))
      raw = raw["recipe"];
    if (!raw.is_object())
      throw Error("配方文件必须是 JSON 对象");
    return raw;
  }
  auto it = profile.capabilities.find(featureKey);
  if (it != profile.capabilities.end() && it->second.recipe.is_object() &&
      !it->second.recipe.empty())
    return it->second.recipe;
  Json fallback;
  fallback["kind"] = "candidate";
  fallback["feature"] = recipeKeyForFeature(featureKey, profile);
  fallback["status"] = "not-locatable";
  return fallback;
}

std::vector<uint32_t> featureAddresses(const EngineProfile &profile,
                                       const PEImage &image,
                                       const ProfileRegistry &registry,
                                       const std::string &featureKey) {
  std::set<uint32_t> unique;
  auto capability = profile.capabilities.find(featureKey);
  if (capability != profile.capabilities.end())
    for (const auto &entry : capability->second.addresses)
      if (entry.second)
        unique.insert(entry.second);
  auto remote = profile.remoteAddresses.find(featureKey);
  if (remote != profile.remoteAddresses.end() && remote->second)
    unique.insert(remote->second);
  try {
    for (const auto &patch : registry.resolvePatches(image, featureKey))
      unique.insert(patch.address);
  } catch (...) {
  }
  if (featureKey == "control_auto_return" &&
      profile.metadata.contains("auto_return_layout") &&
      profile.metadata["auto_return_layout"].is_object()) {
    const auto &layout = profile.metadata["auto_return_layout"];
    if (layout.contains("nativeEffect") &&
        layout["nativeEffect"].is_number_unsigned())
      unique.insert(layout["nativeEffect"].get<uint32_t>());
    for (const char *key : {"callSites", "patchSites"}) {
      if (!layout.contains(key) || !layout[key].is_array())
        continue;
      for (const auto &value : layout[key])
        if (value.is_number_unsigned())
          unique.insert(value.get<uint32_t>());
    }
  }
  return std::vector<uint32_t>(unique.begin(), unique.end());
}

Json commonRiskFacts(const PEImage &image) {
  static const uint32_t riskAddresses[] = {
      0x004242B0, 0x0044E570, 0x0044EA74, 0x00485E00,
      0x00457428, 0x004594DD, 0x00414F65};
  Json windows = Json::array();
  for (auto address : riskAddresses)
    windows.push_back(disassemble(image, address, 8, 32));
  Json literal;
  for (uint32_t value : {0x00510C80u, 0x004B09DBu, 0x004B5DF0u}) {
    std::vector<uint32_t> hits;
    try {
      hits = image.findLiteralReferences(value, true);
    } catch (...) {
    }
    literal[hexAddress(value)] = hits.size();
  }
  Json facts;
  facts["legacyRiskWindows"] = windows;
  facts["literalReferenceCounts"] = literal;
  return facts;
}

Json oracleRiskFacts(const PEImage &image) {
  static const uint32_t riskAddresses[] = {
      0x004242B0, 0x00437BC0, 0x00454405, 0x00455646, 0x0044E570,
      0x0044EA74, 0x00485E00, 0x00457428, 0x004594DD, 0x00414F65};
  Json windows = Json::array();
  for (auto address : riskAddresses)
    windows.push_back(disassemble(image, address, 16, 64));
  Json literal;
  for (uint32_t value : {0x00510C80u, 0x004B09DBu, 0x004B5DF0u}) {
    std::vector<uint32_t> hits;
    try {
      hits = image.findLiteralReferences(value, true);
    } catch (...) {
    }
    literal[hexAddress(value)] = hits.size();
  }
  Json facts;
  facts["riskWindows"] = windows;
  facts["literalReferenceCounts"] = literal;
  return facts;
}

class EvidenceRepository {
public:
  explicit EvidenceRepository(std::wstring root) : root_(std::move(root)) {}

  std::wstring manifestPath() const {
    return (std::filesystem::path(root_) / kEvidenceManifestName).wstring();
  }

  std::map<std::string, std::string> manifestFiles() const {
    std::map<std::string, std::string> result;
    if (!std::filesystem::exists(std::filesystem::path(manifestPath())))
      return result;
    Json manifest;
    try {
      manifest = readJson(manifestPath());
    } catch (const std::exception &error) {
      throw Error(std::string("证据清单无法读取: ") + error.what());
    }
    if (manifest.value("schemaVersion", std::string()) !=
        kEvidenceSchemaVersion)
      throw Error("证据清单版本不受支持");
    if (!manifest.contains("files") || !manifest["files"].is_array())
      throw Error("证据清单缺少文件列表");
    for (const auto &item : manifest["files"]) {
      if (!item.is_object())
        throw Error("证据清单包含非法条目");
      const auto relative = item.value("path", std::string());
      const auto digest = item.value("sha256", std::string());
      if (relative.empty() ||
          std::filesystem::path(relative).is_absolute() ||
          relative.find("..") != std::string::npos)
        throw Error("证据清单包含非法路径");
      if (digest.size() != 64 ||
          !std::all_of(digest.begin(), digest.end(),
                       [](char c) { return isHexDigit(c); }))
        throw Error("证据清单包含非法摘要");
      result[relative] = digest;
    }
    return result;
  }

  std::vector<Json> loadBundles(bool runtimeOnly = false) const {
    std::vector<Json> bundles;
    for (const auto &entry : manifestFiles()) {
      const auto path =
          (std::filesystem::path(root_) / std::filesystem::path(entry.first))
              .wstring();
      Bytes raw;
      try {
        raw = readFile(path);
      } catch (...) {
        throw Error("证据文件缺失: " + entry.first);
      }
      if (sha256(raw) != entry.second)
        throw Error("证据文件清单摘要不匹配: " + entry.first);
      try {
        auto bundle = Json::parse(std::string(raw.begin(), raw.end()));
        evidenceValidateBundle(bundle, runtimeOnly);
        if (!runtimeOnly || bundle.value("phase", std::string()) == "runtime")
          bundles.push_back(std::move(bundle));
      } catch (const std::exception &error) {
        if (runtimeOnly)
          continue;
        throw Error("证据文件无效 " + entry.first + ": " + error.what());
      }
    }
    return bundles;
  }

  Json verifiedRecipe(const std::string &featureKey,
                      const std::vector<std::string> &requiredSha) const {
    std::map<std::string, std::string> required;
    for (const auto &sha : requiredSha)
      required[lowerAscii(sha)] = sha;
    std::map<std::string, std::map<std::string, Json>> candidates;
    for (const auto &bundle : loadBundles(true)) {
      if (bundle.value("featureKey", std::string()) != featureKey)
        continue;
      if (!bundle.contains("target") || !bundle["target"].is_object())
        continue;
      const auto sha = lowerAscii(bundle["target"].value("sha256", std::string()));
      if (!required.count(sha))
        continue;
      candidates[bundle.value("recipeId", std::string())][sha] = bundle;
    }
    for (const auto &candidate : candidates) {
      if (candidate.second.size() != required.size())
        continue;
      std::string first;
      bool consistent = true;
      Json recipe;
      for (const auto &entry : candidate.second) {
        if (!entry.second.contains("recipe") ||
            !entry.second["recipe"].is_object()) {
          consistent = false;
          break;
        }
        const auto canonical = evidenceCanonicalJson(entry.second["recipe"]);
        if (first.empty()) {
          first = canonical;
          recipe = entry.second["recipe"];
        } else if (first != canonical) {
          consistent = false;
          break;
        }
      }
      if (!consistent)
        continue;
      if (evidenceRecipeId(featureKey, recipe) != candidate.first)
        continue;
      Json ids = Json::array();
      for (const auto &entry : candidate.second) {
        const auto digest = entry.second.value("bundleDigest", std::string());
        ids.push_back(entry.first + ":" + digest.substr(0, 16));
      }
      Json result;
      result["featureKey"] = featureKey;
      result["recipeId"] = candidate.first;
      result["recipe"] = recipe;
      result["evidenceIds"] = ids;
      return result;
    }
    return Json();
  }

  std::wstring writeBundle(const Json &bundle) {
    auto finalized = evidenceFinalizeBundle(bundle);
    const bool runtimeVerified =
        finalized.value("phase", std::string()) == "runtime" &&
        finalized.contains("verification") &&
        finalized["verification"].is_object() &&
        finalized["verification"].value("status", std::string()) == "verified";
    evidenceValidateBundle(finalized, runtimeVerified);
    std::filesystem::create_directories(std::filesystem::path(root_));
    const auto feature =
        safeFeatureName(finalized.value("featureKey", std::string()));
    const auto sha = finalized["target"].value("sha256", std::string());
    const auto phase = finalized.value("phase", std::string("unknown"));
    const auto filename =
        feature + "-" + sha.substr(0, 12) + "-" + phase + ".json";
    const auto path = (std::filesystem::path(root_) / std::filesystem::path(filename))
                          .wstring();
    writePrettyJson(path, finalized);
    rebuildManifest();
    return path;
  }

  void rebuildManifest() {
    std::filesystem::create_directories(std::filesystem::path(root_));
    std::vector<std::wstring> names;
    for (const auto &entry :
         std::filesystem::directory_iterator(std::filesystem::path(root_))) {
      if (!entry.is_regular_file())
        continue;
      if (entry.path().extension() != L".json")
        continue;
      if (entry.path().filename() == kEvidenceManifestName)
        continue;
      names.push_back(entry.path().filename().wstring());
    }
    std::sort(names.begin(), names.end());
    Json files = Json::array();
    for (const auto &name : names) {
      const auto path = (std::filesystem::path(root_) / name).wstring();
      Json item;
      item["path"] = utf8(name);
      item["sha256"] = sha256(readFile(path));
      files.push_back(item);
    }
    Json manifest;
    manifest["schemaVersion"] = kEvidenceSchemaVersion;
    manifest["files"] = files;
    const auto path = std::filesystem::path(manifestPath());
    auto temporary = path;
    temporary.replace_extension(L".tmp");
    writePrettyJson(temporary.wstring(), manifest);
    if (!MoveFileExW(temporary.wstring().c_str(), path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      winError("替换证据清单");
  }

private:
  std::wstring root_;
};

ProcessInfo moduleForPid(DWORD pid) {
  Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                           pid));
  if (!snapshot)
    winError("枚举目标模块");
  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Module32FirstW(snapshot.get(), &entry))
    winError("读取目标主模块");
  ProcessInfo info;
  info.pid = pid;
  info.base = uint32_t(reinterpret_cast<uintptr_t>(entry.modBaseAddr));
  info.size = entry.modBaseSize;
  info.path = entry.szExePath;
  return info;
}

struct WatchSpec {
  std::string name;
  uint32_t reference = 0;
  uint32_t size = 0;
};

std::string optionalHex(MemorySession &session, uint32_t address,
                        uint32_t size) {
  try {
    return hex(session.readActual(address, size));
  } catch (...) {
    return "";
  }
}

CONTEXT getThreadContext(DWORD threadId) {
  Handle thread(OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                           FALSE, threadId));
  if (!thread)
    throw Error("打开调试线程失败");
  CONTEXT context{};
  context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  if (!GetThreadContext(thread.get(), &context))
    throw Error("读取调试线程寄存器失败");
  return context;
}

void setThreadContext(DWORD threadId, const CONTEXT &context) {
  Handle thread(OpenThread(THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                           FALSE, threadId));
  if (!thread)
    throw Error("打开调试线程失败");
  auto copy = context;
  if (!SetThreadContext(thread.get(), &copy))
    throw Error("写回调试线程寄存器失败");
}

Json runtimeSample(MemorySession &session, const CONTEXT &context,
                   uint32_t reference, DWORD threadId,
                   const std::vector<WatchSpec> &watches,
                   uint32_t stackSize) {
  Json registers;
  registers["Eax"] = hexAddress(context.Eax);
  registers["Ebx"] = hexAddress(context.Ebx);
  registers["Ecx"] = hexAddress(context.Ecx);
  registers["Edx"] = hexAddress(context.Edx);
  registers["Esi"] = hexAddress(context.Esi);
  registers["Edi"] = hexAddress(context.Edi);
  registers["Ebp"] = hexAddress(context.Ebp);
  registers["Esp"] = hexAddress(context.Esp);
  registers["Eip"] = hexAddress(context.Eip);
  registers["EFlags"] = hexAddress(context.EFlags);
  Json watchValues;
  for (const auto &watch : watches) {
    Json entry;
    entry["address"] = hexAddress(watch.reference);
    entry["size"] = watch.size;
    uint32_t actual = watch.reference;
    try {
      actual = session.actual(watch.reference);
    } catch (...) {
    }
    entry["bytesHex"] = optionalHex(session, actual, watch.size);
    watchValues[watch.name] = entry;
  }
  Json sample;
  sample["threadId"] = uint32_t(threadId);
  sample["breakpoint"] = hexAddress(reference);
  sample["registers"] = registers;
  sample["stackHex"] = optionalHex(session, context.Esp, stackSize);
  sample["watches"] = watchValues;
  return sample;
}

struct DebugSession {
  DWORD pid = 0;
  bool attached = false;
  MemorySession *session = nullptr;
  std::map<uint32_t, Bytes> originals;
  std::set<uint32_t> broken;

  ~DebugSession() {
    for (auto reference : broken) {
      try {
        if (session->read(reference, 1) == Bytes{0xCC})
          session->write(reference, originals[reference]);
      } catch (...) {
      }
    }
    if (attached)
      DebugActiveProcessStop(pid);
  }

  void arm(uint32_t reference) {
    if (!originals.count(reference))
      originals[reference] = session->read(reference, 1);
    session->write(reference, Bytes{0xCC});
    broken.insert(reference);
  }

  void disarm(uint32_t reference) {
    session->write(reference, originals[reference]);
    broken.erase(reference);
  }
};

Json collectRuntimeSamples(DWORD pid, const ProcessInfo &info,
                           MemorySession &session,
                           const std::vector<uint32_t> &breakpoints,
                           const std::vector<WatchSpec> &watches,
                           int sampleCount, double timeoutSeconds,
                           uint32_t stackSize) {
  if (breakpoints.empty())
    throw Error("运行时采集至少需要一个断点");
  std::vector<uint32_t> unique;
  for (auto reference : breakpoints) {
    if (std::find(unique.begin(), unique.end(), reference) == unique.end())
      unique.push_back(reference);
  }
  std::sort(unique.begin(), unique.end());
  if (!DebugActiveProcess(pid))
    throw Error("附加 Windows 调试器失败，WinError=" +
                std::to_string(GetLastError()));
  DebugSession state;
  state.pid = pid;
  state.attached = true;
  state.session = &session;
  DebugSetProcessKillOnExit(FALSE);
  for (auto reference : unique)
    state.arm(reference);

  Json samples = Json::array();
  struct Pending {
    uint32_t reference = 0;
    Json sample;
  };
  std::map<DWORD, Pending> pending;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(
                            int64_t(std::max(0.0, timeoutSeconds) * 1000.0));
  while (int(samples.size()) < sampleCount &&
         std::chrono::steady_clock::now() < deadline) {
    DEBUG_EVENT event{};
    if (!WaitForDebugEvent(&event, 250)) {
      const auto error = GetLastError();
      if (error == 121)
        continue;
      throw Error("等待调试事件失败，WinError=" + std::to_string(error));
    }
    DWORD status = DBG_CONTINUE;
    if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
      ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
      break;
    }
    if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
      const auto code = event.u.Exception.ExceptionRecord.ExceptionCode;
      const auto threadId = event.dwThreadId;
      const auto exceptionAddress = uint32_t(
          reinterpret_cast<uintptr_t>(
              event.u.Exception.ExceptionRecord.ExceptionAddress));
      uint32_t matched = 0;
      bool found = false;
      for (auto reference : unique) {
        if (session.actual(reference) == exceptionAddress) {
          matched = reference;
          found = true;
          break;
        }
      }
      if (code == EXCEPTION_BREAKPOINT && found) {
        auto context = getThreadContext(threadId);
        auto sample =
            runtimeSample(session, context, matched, threadId, watches,
                          stackSize);
        state.disarm(matched);
        context.Eip = exceptionAddress;
        context.EFlags |= 0x100;
        setThreadContext(threadId, context);
        pending[threadId] = {matched, sample};
      } else if (code == EXCEPTION_SINGLE_STEP && pending.count(threadId)) {
        auto current = pending[threadId];
        pending.erase(threadId);
        auto context = getThreadContext(threadId);
        context.EFlags &= ~0x100;
        setThreadContext(threadId, context);
        current.sample["nextEip"] = hexAddress(context.Eip);
        state.arm(current.reference);
        samples.push_back(current.sample);
      } else {
        status = DBG_EXCEPTION_NOT_HANDLED;
      }
    }
    ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
  }
  if (int(samples.size()) < sampleCount)
    throw Error("采集超时，只获得 " + std::to_string(samples.size()) + "/" +
                std::to_string(sampleCount) + " 个样本");
  (void)info;
  return samples;
}

struct CliArgs {
  std::wstring command;
  std::wstring positional;
  std::map<std::wstring, std::vector<std::wstring>> options;

  bool has(const std::wstring &name) const {
    return options.count(name) != 0;
  }
  std::wstring one(const std::wstring &name,
                   const std::wstring &fallback = L"") const {
    auto it = options.find(name);
    if (it == options.end() || it->second.empty())
      return fallback;
    return it->second.front();
  }
  std::vector<std::wstring> many(const std::wstring &name) const {
    auto it = options.find(name);
    return it == options.end() ? std::vector<std::wstring>{} : it->second;
  }
};

CliArgs parseArgs(int argc, wchar_t **argv) {
  CliArgs args;
  args.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::wstring value = argv[i];
    if (value.rfind(L"--", 0) == 0) {
      if (i + 1 < argc && std::wstring(argv[i + 1]).rfind(L"--", 0) != 0) {
        args.options[value].push_back(argv[i + 1]);
        ++i;
      } else {
        args.options[value].push_back(L"true");
      }
    } else {
      args.positional = value;
    }
  }
  return args;
}

uint32_t parseU32(const std::wstring &text, const char *field) {
  if (text.empty())
    throw Error(std::string(field) + "不能为空");
  size_t consumed = 0;
  auto value = std::stoull(text, &consumed, 0);
  if (consumed != text.size() || value > 0xffffffffull)
    throw Error(std::string(field) + "数值越界");
  return uint32_t(value);
}

int parseInt(const std::wstring &text, const char *field) {
  if (text.empty())
    throw Error(std::string(field) + "不能为空");
  size_t consumed = 0;
  auto value = std::stoll(text, &consumed, 0);
  if (consumed != text.size() || value < INT_MIN || value > INT_MAX)
    throw Error(std::string(field) + "数值越界");
  return int(value);
}

WatchSpec parseWatch(const std::wstring &text) {
  const auto utf = utf8(text);
  const auto first = utf.find(':');
  const auto second = first == std::string::npos ? std::string::npos
                                                 : utf.find(':', first + 1);
  if (first == std::string::npos || second == std::string::npos)
    throw Error("观察区格式应为 name:address:size");
  WatchSpec watch;
  watch.name = utf.substr(0, first);
  if (watch.name.empty())
    throw Error("观察区名称不能为空");
  watch.reference = parseU32(wide(utf.substr(first + 1, second - first - 1)),
                             "观察区地址");
  watch.size = parseU32(wide(utf.substr(second + 1)), "观察区长度");
  if (watch.size == 0 || watch.size > 0x10000)
    throw Error("观察区长度无效");
  return watch;
}

std::pair<std::string, std::string> parseTag(const std::wstring &text) {
  const auto utf = utf8(text);
  const auto separator = utf.find('=');
  if (separator == std::string::npos || separator == 0 ||
      separator + 1 == utf.size())
    throw Error("标签格式应为 name=value");
  return {utf.substr(0, separator), utf.substr(separator + 1)};
}

std::wstring evidenceOutput(const CliArgs &args) {
  auto output = args.one(L"--output");
  if (output.empty())
    throw Error("缺少 --output");
  return output;
}

int runStatic(const CliArgs &args) {
  auto exe = args.one(L"--exe");
  if (exe.empty())
    throw Error("缺少 --exe");
  auto output = evidenceOutput(args);
  PEImage image(exe);
  ProfileRegistry registry;
  auto profile = registry.detect(exe);
  std::vector<std::string> features;
  for (const auto &value : args.many(L"--feature"))
    features.push_back(utf8(value));
  if (features.empty())
    for (const auto &entry : profile.capabilities)
      features.push_back(entry.first);
  std::sort(features.begin(), features.end());
  const auto common = commonRiskFacts(image);
  EvidenceRepository repository(output);
  for (const auto &feature : features) {
    if (!profile.capabilities.count(feature))
      throw Error("画像中不存在功能: " + feature);
    const auto &capability = profile.capabilities.at(feature);
    Json recipe;
    recipe = loadRecipe(args.one(L"--recipe"), feature, profile);
    Json addresses = Json::array();
    Json windows = Json::array();
    for (auto address : featureAddresses(profile, image, registry, feature)) {
      addresses.push_back(hexAddress(address));
      windows.push_back(disassemble(image, address));
    }
    Json evidence;
    evidence["profileState"] = capability.state.empty() ? "Enabled"
                                                       : capability.state;
    evidence["reasonCode"] = capability.reasonCode;
    evidence["addresses"] = addresses;
    evidence["windows"] = windows;
    evidence["commonRiskFacts"] = common;
    auto bundle = makeBundle(feature, image.sha256(), fileLength(exe),
                             evidenceRecipeId(feature, recipe), recipe,
                             "static", evidence, Json::array(),
                             defaultVerification());
    std::cout << utf8(repository.writeBundle(bundle)) << "\n";
  }
  return 0;
}

int runOracle(const CliArgs &args) {
  auto exe = args.one(L"--exe");
  if (exe.empty())
    throw Error("缺少 --exe");
  auto output = evidenceOutput(args);
  const int legacyIndex = args.has(L"--legacy-index")
                              ? parseInt(args.one(L"--legacy-index"), "旧版索引")
                              : 0;
  if (legacyIndex < 0 || legacyIndex > 4)
    throw Error("旧版索引必须在 0..4");
  const auto battleAddress =
      args.has(L"--battle-address")
          ? parseU32(args.one(L"--battle-address"), "战场地址")
          : 0x004A7B20u;
  const auto battleStride =
      args.has(L"--battle-stride")
          ? parseU32(args.one(L"--battle-stride"), "战场步长")
          : 0x30u;
  PEImage image(exe);
  Bytes stub;
  const char *source = "file-backed";
  try {
    stub = image.readVA(0x00485E00, 0x49);
  } catch (...) {
    stub.assign(0x49, 0);
    source = "loader-zero-filled-uninitialized-section";
  }
  Bytes reconstructed(0x49);
  {
    if (stub.size() < 0x49)
      throw Error("旧自动回归代码洞样本不足 0x49 字节");
    reconstructed = stub;
    overlayU32(reconstructed, 0x00, 0x83EC8B55);
    overlayU32(reconstructed, 0x04, 0x75FF04EC);
    if (legacyIndex >= 3) {
      overlayU32(reconstructed, 0x08, 0x9E8DE808);
      overlayU32(reconstructed, 0x0C, 0x4D8B0004);
    } else {
      overlayU32(reconstructed, 0x08, 0xCF44E808);
      overlayU32(reconstructed, 0x0C, 0x4D8BFFF8);
    }
    overlayU32(reconstructed, 0x10, 0xFFE18108);
    overlayU32(reconstructed, 0x17, 0xC96B, 2);
    overlayU32(reconstructed, 0x19, battleStride);
    overlayU32(reconstructed, 0x1A, 0x81, 1);
    overlayU32(reconstructed, 0x1B, 0xC1, 1);
    overlayU32(reconstructed, 0x1C, battleAddress, 3);
    overlayU32(reconstructed, 0x20, 0xB8FC4D89);
    overlayU32(reconstructed, 0x24, 0x06, 1);
    overlayU32(reconstructed, 0x28, 0x4120D0F7);
    overlayU32(reconstructed, 0x2C, 0x0D, 1);
    if (legacyIndex >= 1) {
      overlayU32(reconstructed, 0x2D, 0xE831FF, 3);
      overlayU32(reconstructed, 0x30, 0xFFFB86A2);
      overlayU32(reconstructed, 0x34, 0x00008868);
      overlayU32(reconstructed, 0x38, 0x50066A00);
    } else {
      overlayU32(reconstructed, 0x2D, 0x00008868);
      overlayU32(reconstructed, 0x31, 0x8B066A00);
      overlayU32(reconstructed, 0x35, 0x71FFFC4D);
      overlayU32(reconstructed, 0x39, 0x00909004, 3);
    }
    overlayU32(reconstructed, 0x3C, 0xE8, 1);
    overlayU32(reconstructed, 0x3D, 0xFFFB8869);
    overlayU32(reconstructed, 0x41, 0xFE5DE58B);
    overlayU32(reconstructed, 0x45, 0x00C3FC4D);
  }
  Json recipe;
  recipe["kind"] = "legacy-oracle";
  recipe["legacyIndex"] = legacyIndex;
  recipe["battleAddress"] = battleAddress;
  recipe["battleStride"] = battleStride;
  recipe["riskAddresses"] = Json::array(
      {0x004242B0u, 0x00437BC0u, 0x00454405u, 0x00455646u, 0x0044E570u,
       0x0044EA74u, 0x00485E00u, 0x00457428u, 0x004594DDu, 0x00414F65u});
  Json evidence = oracleRiskFacts(image);
  Json stubEvidence;
  stubEvidence["address"] = hexAddress(0x00485E00);
  stubEvidence["originalSource"] = source;
  stubEvidence["originalHex"] = hex(stub);
  stubEvidence["reconstructedHex"] = hex(reconstructed);
  stubEvidence["reconstructedDisassembly"] =
      disassembleRaw(reconstructed, 0x00485E00);
  Json changed = Json::array();
  for (size_t i = 0; i < reconstructed.size(); ++i)
    if (stub[i] != reconstructed[i])
      changed.push_back(i);
  stubEvidence["changedOffsets"] = changed;
  evidence["autoReturnStub"] = stubEvidence;
  EvidenceRepository repository(output);
  auto bundle = makeBundle("legacy_oracle", image.sha256(), fileLength(exe),
                           evidenceRecipeId("legacy_oracle", recipe), recipe,
                           "static", evidence, Json::array(),
                           defaultVerification());
  std::cout << utf8(repository.writeBundle(bundle)) << "\n";
  return 0;
}

int runRuntime(const CliArgs &args) {
  if (!args.has(L"--pid"))
    throw Error("缺少 --pid");
  const auto pid = DWORD(parseU32(args.one(L"--pid"), "进程号"));
  auto feature = utf8(args.one(L"--feature"));
  if (feature.empty())
    throw Error("缺少 --feature");
  auto output = evidenceOutput(args);
  std::vector<uint32_t> breakpoints;
  for (const auto &value : args.many(L"--breakpoint"))
    breakpoints.push_back(parseU32(value, "断点地址"));
  std::vector<WatchSpec> watches;
  for (const auto &value : args.many(L"--watch"))
    watches.push_back(parseWatch(value));
  const auto sampleCount =
      args.has(L"--samples") ? parseInt(args.one(L"--samples"), "样本数") : 6;
  if (sampleCount <= 0)
    throw Error("样本数必须为正");
  const auto timeout =
      args.has(L"--timeout") ? std::stod(args.one(L"--timeout")) : 120.0;
  const auto stackSize = args.has(L"--stack-bytes")
                             ? parseU32(args.one(L"--stack-bytes"), "栈字节")
                             : 64u;
  const auto info = moduleForPid(pid);
  if (info.path.empty())
    throw Error("无法确定目标主模块路径");
  PEImage image(info.path);
  ProfileRegistry registry;
  auto profile = registry.detect(info.path);
  auto recipe = loadRecipe(args.one(L"--recipe"), feature, profile);
  auto backend = std::make_shared<Win32MemoryBackend>(pid);
  MemorySession session(backend, info.base, info.size, pid);
  session.referenceBase = profile.imageBase;
  session.imagePath = info.path;
  auto samples = collectRuntimeSamples(pid, info, session, breakpoints, watches,
                                       sampleCount, timeout, stackSize);
  std::map<std::string, std::string> tags;
  for (const auto &value : args.many(L"--tag")) {
    auto tag = parseTag(value);
    tags[tag.first] = tag.second;
  }
  for (auto &sample : samples) {
    Json tagObject;
    tagObject["base"] = profile.variantName;
    for (const auto &entry : tags)
      tagObject[entry.first] = entry.second;
    sample["tags"] = tagObject;
  }
  auto append = args.one(L"--append");
  if (!append.empty()) {
    auto previous = readJson(append);
    evidenceValidateBundle(previous, false);
    if (previous.value("phase", std::string()) != "runtime" ||
        previous.value("featureKey", std::string()) != feature ||
        lowerAscii(previous["target"].value("sha256", std::string())) !=
            lowerAscii(image.sha256()) ||
        previous.value("recipeId", std::string()) !=
            evidenceRecipeId(feature, recipe))
      throw Error("追加证据的目标、功能或配方不一致");
    Json combined = Json::array();
    if (previous.contains("runtimeSamples") &&
        previous["runtimeSamples"].is_array())
      for (const auto &sample : previous["runtimeSamples"])
        combined.push_back(sample);
    for (const auto &sample : samples)
      combined.push_back(sample);
    samples = combined;
  }
  Json staticEvidence;
  Json names = Json::array();
  for (auto reference : breakpoints)
    names.push_back(hexAddress(reference));
  staticEvidence["breakpoints"] = names;
  Json watchList = Json::array();
  for (const auto &watch : watches)
    watchList.push_back(Json::array(
        {watch.name, watch.reference, watch.size}));
  staticEvidence["watches"] = watchList;
  Json verification = defaultVerification();
  verification["checks"] = Json::object();
  for (const auto &name : requiredRuntimeChecks(feature))
    verification["checks"][name] = false;
  EvidenceRepository repository(output);
  auto bundle = makeBundle(feature, image.sha256(), fileLength(info.path),
                           evidenceRecipeId(feature, recipe), recipe,
                           "runtime", staticEvidence, samples, verification);
  std::cout << utf8(repository.writeBundle(bundle)) << "\n";
  return 0;
}

int runInspect(const CliArgs &args) {
  if (args.positional.empty())
    throw Error("缺少证据路径");
  auto bundle = readJson(args.positional);
  evidenceValidateBundle(bundle, false);
  Json checks = Json::object();
  if (bundle.contains("verification") && bundle["verification"].is_object() &&
      bundle["verification"].contains("checks") &&
      bundle["verification"]["checks"].is_object())
    checks = bundle["verification"]["checks"];
  std::vector<std::string> missing;
  for (const auto &name :
       requiredRuntimeChecks(bundle.value("featureKey", std::string())))
    if (!checks.contains(name) || checks[name] != true)
      missing.push_back(name);
  std::cout << "feature=" << bundle.value("featureKey", std::string())
            << " phase=" << bundle.value("phase", std::string())
            << " digest=" << bundle.value("bundleDigest", std::string())
            << "\n";
  if (!missing.empty()) {
    std::cout << "not verified; missing automated checks: ";
    for (size_t i = 0; i < missing.size(); ++i)
      std::cout << (i ? ", " : "") << missing[i];
    std::cout << "\n";
    return 2;
  }
  try {
    evidenceValidateBundle(bundle, true);
  } catch (const std::exception &error) {
    std::cout << "not verified: " << error.what() << "\n";
    return 2;
  }
  std::cout << "runtime evidence verified\n";
  return 0;
}

int runVerify(const CliArgs &args) {
  if (args.positional.empty())
    throw Error("缺少证据路径");
  auto output = evidenceOutput(args);
  auto bundle = readJson(args.positional);
  evidenceValidateBundle(bundle, false);
  if (bundle.value("phase", std::string()) != "runtime")
    throw Error("只有运行时证据可以执行自动验证");
  auto checks = evidenceEvaluateVerificationContract(bundle);
  bool passed = !checks.empty();
  std::string missing;
  for (auto it = checks.begin(); it != checks.end(); ++it) {
    if (!it.value().get<bool>()) {
      passed = false;
      missing += (missing.empty() ? "" : ", ") + it.key();
    }
  }
  if (!passed)
    throw Error("验证合同未通过: " + missing);
  Json verification;
  verification["collector"] = kCollector;
  verification["status"] = "verified";
  verification["automated"] = true;
  verification["checks"] = checks;
  bundle["verification"] = verification;
  auto finalized = evidenceFinalizeBundle(bundle);
  evidenceValidateBundle(finalized, true);
  EvidenceRepository repository(output);
  std::cout << utf8(repository.writeBundle(finalized)) << "\n";
  return 0;
}

int runManifest(const CliArgs &args) {
  auto output = evidenceOutput(args);
  EvidenceRepository repository(output);
  repository.rebuildManifest();
  return 0;
}

} // namespace

namespace {
nlohmann::json sortedJson(const Json &value) {
  if (value.is_object()) {
    nlohmann::json result = nlohmann::json::object();
    for (auto it = value.begin(); it != value.end(); ++it)
      result[it.key()] = sortedJson(it.value());
    return result;
  }
  if (value.is_array()) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto &item : value)
      result.push_back(sortedJson(item));
    return result;
  }
  return nlohmann::json::parse(value.dump(-1, ' ', false));
}
} // namespace

std::string evidenceCanonicalJson(const Json &value) {
  return sortedJson(value).dump(-1, ' ', false);
}

std::string evidenceDigest(const Json &value) {
  const auto text = evidenceCanonicalJson(value);
  return sha256(Bytes(text.begin(), text.end()));
}

std::string evidenceRecipeId(const std::string &featureKey,
                             const Json &recipe) {
  return "ccz66." + featureKey + "." + evidenceDigest(recipe).substr(0, 20);
}

std::vector<std::string>
evidenceRequiredRuntimeChecks(const std::string &featureKey) {
  return requiredRuntimeChecks(featureKey);
}

Json evidenceFinalizeBundle(const Json &bundle) {
  Json result = bundle;
  result.erase("bundleDigest");
  if (!result.contains("runtimeSamples") ||
      !result["runtimeSamples"].is_array())
    result["runtimeSamples"] = Json::array();
  result["rawSampleDigest"] = evidenceDigest(result["runtimeSamples"]);
  result["bundleDigest"] = evidenceDigest(result);
  return result;
}

void evidenceValidateBundle(const Json &bundle, bool runtimeRequired) {
  if (bundle.value("schemaVersion", std::string()) != kEvidenceSchemaVersion)
    throw Error("证据 schemaVersion 不受支持");
  if (bundle.value("featureKey", std::string()).empty() ||
      bundle.value("recipeId", std::string()).empty())
    throw Error("证据缺少功能键或配方 ID");
  if (!bundle.contains("target") || !bundle["target"].is_object())
    throw Error("证据目标无效");
  const auto sha = bundle["target"].value("sha256", std::string());
  if (sha.size() != 64 ||
      !std::all_of(sha.begin(), sha.end(),
                   [](char c) { return isHexDigit(c); }))
    throw Error("证据目标 SHA256 无效");
  if (bundle["target"].value("fileSize", 0ull) == 0)
    throw Error("证据目标文件长度无效");
  auto expected = bundle;
  const auto digest = expected.value("bundleDigest", std::string());
  expected.erase("bundleDigest");
  if (digest.empty() ||
      lowerAscii(evidenceDigest(expected)) != lowerAscii(digest))
    throw Error("证据内容摘要不匹配");
  Json samples = Json::array();
  if (bundle.contains("runtimeSamples")) {
    if (!bundle["runtimeSamples"].is_array())
      throw Error("证据原始样本无效");
    samples = bundle["runtimeSamples"];
  }
  if (lowerAscii(evidenceDigest(samples)) !=
      lowerAscii(bundle.value("rawSampleDigest", std::string())))
    throw Error("证据原始样本摘要不匹配");
  if (!runtimeRequired)
    return;
  if (bundle.value("phase", std::string()) != "runtime")
    throw Error("证据不是运行时阶段");
  if (!bundle.contains("verification") || !bundle["verification"].is_object())
    throw Error("运行时证据缺少验证结果");
  const auto &verification = bundle["verification"];
  if (verification.value("collector", std::string()) != kCollector)
    throw Error("运行时证据不是由受信采集器生成");
  if (verification.value("status", std::string()) != "verified" ||
      !verification.value("automated", false))
    throw Error("运行时证据没有通过自动验证");
  if (!verification.contains("checks") || !verification["checks"].is_object())
    throw Error("运行时证据缺少自动检查");
  const auto &checks = verification["checks"];
  std::string missing;
  for (const auto &name :
       requiredRuntimeChecks(bundle.value("featureKey", std::string())))
    if (!checks.contains(name) || checks[name] != true)
      missing += (missing.empty() ? "" : ", ") + name;
  if (!missing.empty())
    throw Error("运行时证据未通过: " + missing);
  if (samples.empty())
    throw Error("运行时证据没有原始样本");
}

Json evidenceEvaluateVerificationContract(const Json &bundle) {
  Json checks = Json::object();
  const auto feature = bundle.value("featureKey", std::string());
  Json declared = Json::object();
  if (bundle.contains("recipe") && bundle["recipe"].is_object() &&
      bundle["recipe"].contains("verificationContract") &&
      bundle["recipe"]["verificationContract"].is_object() &&
      bundle["recipe"]["verificationContract"].contains("checks") &&
      bundle["recipe"]["verificationContract"]["checks"].is_object())
    declared = bundle["recipe"]["verificationContract"]["checks"];
  Json samples = Json::array();
  if (bundle.contains("runtimeSamples") &&
      bundle["runtimeSamples"].is_array())
    samples = bundle["runtimeSamples"];
  for (const auto &name : requiredRuntimeChecks(feature)) {
    const Json predicate =
        declared.contains(name) ? declared[name] : Json();
    checks[name] = predicate.is_object() &&
                   evaluatePredicate(predicate, samples);
  }
  return checks;
}

Json evidenceSummary(const EngineProfile &profile, const ProcessInfo &process) {
  Json j;
  j["schema"] = 2;
  j["pid"] = process.pid;
  j["created"] = process.created;
  j["path"] = utf8(process.path);
  j["base"] = process.base;
  j["imageSize"] = process.size;
  Json profileJson;
  profileJson["displayVersion"] = profile.displayVersion;
  profileJson["variant"] = profile.variantName;
  profileJson["identity"] = profile.identity;
  profileJson["limits"] = profile.limits;
  profileJson["addresses"] = profile.addresses;
  profileJson["capabilities"] = Json::object();
  for (const auto &item : profile.capabilities) {
    Json capability;
    capability["read"] = item.second.readable;
    capability["write"] = item.second.writable;
    capability["state"] = item.second.state;
    capability["reason"] = item.second.reason;
    capability["reasonCode"] = item.second.reasonCode;
    profileJson["capabilities"][item.first] = capability;
  }
  profileJson["consumables"] = {
      {"key", profile.consumables.key},
      {"base", profile.consumables.base},
      {"idMin", profile.consumables.idMin},
      {"idMax", profile.consumables.idMax},
      {"storageCount", profile.consumables.storageCount},
      {"itemIdCount", profile.consumables.itemIds.size()},
  };
  profileJson["variables"] = {
      {"boolStorage",
       profile.boolVariables.storage == VariableStorage::Bitset ? "bitset"
                                                                : "array"},
      {"boolRecordCount", profile.boolVariables.recordCount},
      {"boolWritableCount", profile.boolVariables.writableCount},
      {"boolBrowseCount", profile.boolVariables.browseCount},
      {"intRecordCount", profile.intVariables.recordCount},
      {"ptrRecordCount", profile.ptrVariables.recordCount},
      {"ptrBase", profile.ptrVariables.base},
      {"diagnostics",
       profile.metadata.value("boolean_diagnostics", Json::object())},
  };
  j["profile"] = profileJson;
  return j;
}

int evidenceCli(int argc, wchar_t **argv) {
  try {
    if (argc < 2)
      return 2;
    const std::wstring command = argv[1];
    if (command == L"--evidence") {
      if (argc < 3)
        return 2;
      const std::wstring path = argv[2];
      PEImage image(path);
      ProfileRegistry registry;
      auto profile = registry.detect(path);
      Json j;
      j["imageSha256"] = image.sha256();
      j["imageBase"] = image.imageBase();
      j["imageSize"] = image.imageSize();
      j["x86"] = image.isX86();
      j["profile"] = evidenceSummary(profile, ProcessInfo{})["profile"];
      std::cout << j.dump(2) << "\n";
      return 0;
    }
    const auto args = parseArgs(argc, argv);
    if (command == L"--evidence-static")
      return runStatic(args);
    if (command == L"--evidence-oracle")
      return runOracle(args);
    if (command == L"--evidence-runtime")
      return runRuntime(args);
    if (command == L"--evidence-inspect")
      return runInspect(args);
    if (command == L"--evidence-verify")
      return runVerify(args);
    if (command == L"--evidence-manifest")
      return runManifest(args);
    return 2;
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
} // namespace banshou
