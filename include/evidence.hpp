#pragma once
#include "session.hpp"
#include <string>
namespace banshou {
constexpr const char *kEvidenceSchemaVersion = "1.0";
constexpr const char *kEvidenceCollectorId = "wrench-evidence-collector/1";
constexpr const char *kEvidenceManifestName = "manifest.json";

std::string evidenceCanonicalJson(const Json &value);
std::string evidenceDigest(const Json &value);
std::string evidenceRecipeId(const std::string &featureKey,
                             const Json &recipe);
std::vector<std::string> evidenceRequiredRuntimeChecks(
    const std::string &featureKey);
Json evidenceFinalizeBundle(const Json &bundle);
void evidenceValidateBundle(const Json &bundle, bool runtimeRequired = false);
Json evidenceEvaluateVerificationContract(const Json &bundle);
Json evidenceSummary(const EngineProfile &profile, const ProcessInfo &process);
int evidenceCli(int argc, wchar_t **argv);
} // namespace banshou
