#include "diy.hpp"

namespace banshou {
namespace {
struct ParsedRow {
  std::array<std::string, 6> columns{};
  std::vector<std::string> trailing;
};

std::vector<ParsedRow> parseCsv(const std::string &input) {
  std::vector<ParsedRow> out;
  ParsedRow row;
  std::string field;
  bool quoted = false;
  size_t column = 0;

  auto pushField = [&] {
    if (column < row.columns.size())
      row.columns[column] = field;
    else
      row.trailing.push_back(field);
    ++column;
    field.clear();
  };
  auto finishRow = [&] {
    pushField();
    while (column < row.columns.size())
      row.columns[column++].clear();
    out.push_back(std::move(row));
    row = ParsedRow{};
    column = 0;
  };

  for (size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < input.size() && input[i + 1] == '"') {
          field.push_back('"');
          ++i;
        } else {
          quoted = false;
        }
      } else {
        field.push_back(c);
      }
    } else if (c == '"' && field.empty()) {
      quoted = true;
    } else if (c == ',') {
      pushField();
    } else if (c == '\n') {
      if (!field.empty() && field.back() == '\r')
        field.pop_back();
      finishRow();
    } else {
      field.push_back(c);
    }
  }
  if (quoted)
    throw Error("DIY.csv 引号未闭合");
  if (!field.empty() || column != 0 || !row.trailing.empty())
    finishRow();
  return out;
}

std::string csvEscape(const std::string &value) {
  const bool quote = value.find_first_of(",\"\r\n") != std::string::npos;
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    if (c == '"')
      escaped += "\"\"";
    else
      escaped.push_back(c);
  }
  return quote ? "\"" + escaped + "\"" : escaped;
}
} // namespace

void DiyCsv::load(const std::wstring &path) {
  auto data = readFile(path);
  std::string text(data.begin(), data.end());
  if (text.size() >= 3 && uint8_t(text[0]) == 0xef &&
      uint8_t(text[1]) == 0xbb && uint8_t(text[2]) == 0xbf)
    text.erase(0, 3);
  auto parsed = parseCsv(text);
  rows_.clear();
  sectionStarts_.clear();
  for (size_t i = 0; i < parsed.size(); ++i) {
    DiyRow row;
    row.columns = std::move(parsed[i].columns);
    row.trailingColumns = std::move(parsed[i].trailing);
    row.sourceLine = i + 1;
    rows_.push_back(std::move(row));
    bool section = !rows_.back().columns[0].empty();
    for (size_t j = 1; j < 6; ++j)
      section = section && rows_.back().columns[j].empty();
    section = section && rows_.back().trailingColumns.empty();
    if (section)
      sectionStarts_.push_back(i);
  }
  if (rows_.empty())
    throw Error("DIY.csv 为空");
}

void DiyCsv::save(const std::wstring &path) const {
  std::string out = "\xEF\xBB\xBF";
  for (const auto &row : rows_) {
    for (size_t i = 0; i < row.columns.size(); ++i) {
      if (i)
        out += ',';
      out += csvEscape(row.columns[i]);
    }
    for (const auto &column : row.trailingColumns) {
      out += ',';
      out += csvEscape(column);
    }
    out += "\r\n";
  }
  writeText(path, out);
}

std::vector<DiyRow> DiyCsv::page(size_t offset, size_t count) const {
  if (offset > rows_.size())
    return {};
  const auto available = rows_.size() - offset;
  const auto end = offset + std::min(available, count);
  return std::vector<DiyRow>(rows_.begin() + offset, rows_.begin() + end);
}

std::vector<DiyExpandedRow> DiyCsv::expand(size_t section, size_t offset,
                                           size_t count) const {
  std::vector<DiyExpandedRow> out;
  if (count == 0 || section >= sectionStarts_.size())
    return out;
  const size_t start = sectionStarts_[section] + 1;
  const size_t end = section + 1 < sectionStarts_.size()
                         ? sectionStarts_[section + 1]
                         : rows_.size();
  size_t seen = 0;
  for (size_t i = start; i < end; ++i) {
    const auto &row = rows_[i];
    if (row.columns[2].empty())
      continue;
    uint32_t base = 0, width = 0, repeats = 1;
    try {
      size_t consumed = 0;
      auto parsed = std::stoull(row.columns[2], &consumed, 16);
      if (consumed != row.columns[2].size() || parsed > UINT32_MAX)
        continue;
      base = static_cast<uint32_t>(parsed);
      parsed = std::stoull(row.columns[3], &consumed, 10);
      if (consumed != row.columns[3].size() || parsed > 4)
        continue;
      width = static_cast<uint32_t>(parsed);
      parsed = std::stoull(row.columns[4], &consumed, 10);
      if (consumed != row.columns[4].size() || parsed == 0 ||
          parsed > UINT32_MAX)
        continue;
      repeats = static_cast<uint32_t>(parsed);
    } catch (...) {
      continue;
    }
    if (width != 1 && width != 2 && width != 4)
      continue;
    for (size_t n = 0; n < repeats; ++n) {
      const auto address = uint64_t(base) + uint64_t(n) * width;
      if (address > UINT32_MAX)
        continue;
      if (seen < offset) {
        ++seen;
        continue;
      }
      ++seen;
      if (out.size() >= count)
        return out;
      DiyExpandedRow expanded;
      expanded.sourceRow = i;
      expanded.repeatIndex = n;
      expanded.address = static_cast<uint32_t>(address);
      expanded.width = static_cast<uint8_t>(width);
      expanded.label = row.columns[0] +
                       (repeats == 1 ? "" : "_" + std::to_string(n));
      expanded.kind = row.columns[1];
      expanded.explanation = row.columns[5];
      out.push_back(std::move(expanded));
    }
  }
  return out;
}

std::vector<std::string> DiyCsv::sectionNames() const {
  std::vector<std::string> out;
  for (auto index : sectionStarts_)
    out.push_back(rows_[index].columns[0]);
  return out;
}
} // namespace banshou
