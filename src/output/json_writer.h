#pragma once

#include <string>

#include "core/analyzer.h"

// Serialization of an analysis result into the frontend data contract
// (docs/frontend-contract.md). Pure formatting: no analysis happens here, and
// nothing is added to or removed from what analyze() produced.

constexpr int kContractVersion = 1;

// Appends `text` as a JSON string literal (with quotes). `text` must be valid
// UTF-8; bytes >= 0x80 are copied through unchanged.
void appendJsonString(std::string& out, const std::string& text);

// Structural UTF-8 check (lead/continuation bytes; does not reject overlong or
// surrogate encodings). Used to refuse input that JSON cannot represent.
bool isValidUtf8(const std::string& text);

// Compact JSON of the contract response for `source` and its AnalysisResult.
// Invariant preserved from the analyzer: !result.ok() => parseTree and ast are null.
std::string analysisToJson(const std::string& source, const AnalysisResult& result);
