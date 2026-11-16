#pragma once
#include "memory.hpp"
#include <cstddef>

namespace banshou {
struct PESection { std::string name; uint32_t va = 0, virtualSize = 0, raw = 0, rawSize = 0, characteristics = 0; bool executable() const { return (characteristics & 0x20000000u) != 0; } };

class MaskedSignature {
public:
    Bytes bytes; std::vector<uint8_t> mask;
    static MaskedSignature parse(const std::string& text);
    std::vector<uint32_t> findAll(const Bytes& data, uint32_t base = 0) const;
};
std::vector<size_t> disassembleX86(const Bytes& code,uint32_t address=0);

class PEImage {
protected:
    Bytes image_;
    std::vector<PESection> sections_;
    uint32_t imageBase_ = 0, imageSize_ = 0, headerSize_ = 0;
    uint16_t machine_ = 0;
public:
    explicit PEImage(const std::wstring& path);
    PEImage() = default;
    virtual ~PEImage() = default;
    const Bytes& bytes() const { return image_; }
    const std::vector<PESection>& sections() const { return sections_; }
    uint32_t imageBase() const { return imageBase_; }
    uint32_t imageSize() const { return imageSize_; }
    bool isX86() const;
    std::string sha256() const { return banshou::sha256(image_); }
    uint32_t vaToOffset(uint32_t va) const;
    virtual Bytes readVA(uint32_t va, size_t size) const;
    virtual Bytes sectionData(const PESection& section) const;
    std::vector<uint32_t> findSignature(const MaskedSignature& sig, bool executableOnly = true) const;
    std::vector<uint32_t> findLiteralReferences(uint32_t value, bool executableOnly = true) const;
    virtual uint32_t operandReference(uint32_t operand) const { return operand; }
    std::function<bool()> cancelled;
};

class RuntimeModuleImage : public PEImage {
    std::shared_ptr<MemorySession> session_;
    mutable std::map<uint32_t, Bytes> mapped_;
public:
    RuntimeModuleImage(const std::wstring& path, std::shared_ptr<MemorySession> session);
    void refresh() { mapped_.clear(); }
    Bytes readVA(uint32_t va, size_t size) const override;
    Bytes sectionData(const PESection& section) const override;
    uint32_t operandReference(uint32_t operand) const override;
};
}
