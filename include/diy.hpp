#pragma once
#include "platform.hpp"
#include <array>
namespace banshou {
struct DiyRow {
    std::array<std::string,6> columns{};
    std::vector<std::string> trailingColumns;
    size_t sourceLine=0;
};
struct DiyExpandedRow { size_t sourceRow=0, repeatIndex=0; uint32_t address=0, value=0; uint8_t width=0; std::string label, kind, explanation; };
class DiyCsv {
    std::vector<DiyRow> rows_; std::vector<size_t> sectionStarts_;
public:
    void load(const std::wstring& path);
    void save(const std::wstring& path) const;
    const std::vector<DiyRow>& rows() const{return rows_;}
    std::vector<DiyRow> page(size_t offset,size_t count) const;
    std::vector<DiyExpandedRow> expand(size_t section,size_t offset,size_t count) const;
    size_t sectionCount()const{return sectionStarts_.size();}
    std::vector<std::string> sectionNames()const;
};
}
