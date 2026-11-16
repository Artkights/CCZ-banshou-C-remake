#pragma once
#include "platform.hpp"
#include <map>
namespace banshou {
struct ProcessInfo { DWORD pid = 0; uint64_t created = 0; uint32_t base = 0, size = 0; std::wstring path; HWND window = nullptr; };
struct CompareWriteEntry {
  uint32_t address = 0;
  Bytes expected, replacement;
  bool actualAddress = false;
  bool code = false;
};
ProcessInfo locateProcess(HWND window);
class IMemoryBackend {
public:
    virtual ~IMemoryBackend() = default;
    virtual Bytes read(uint32_t address, size_t size) = 0;
    virtual void write(uint32_t address, const Bytes& data) = 0;
    virtual bool alive() const = 0;
    virtual DWORD protect(uint32_t address, size_t size, DWORD protection) = 0;
    virtual void flush(uint32_t address, size_t size) = 0;
    virtual uint32_t allocate(size_t size) = 0;
    virtual void free(uint32_t address) = 0;
};
class Win32MemoryBackend final : public IMemoryBackend {
    Handle handle_;
public:
    explicit Win32MemoryBackend(DWORD pid, bool readOnly = false);
    HANDLE handle() const { return handle_.get(); }
    Bytes read(uint32_t address, size_t size) override;
    void write(uint32_t address, const Bytes& data) override;
    bool alive() const override;
    DWORD protect(uint32_t address, size_t size, DWORD protection) override;
    void flush(uint32_t address, size_t size) override;
    uint32_t allocate(size_t size) override;
    void free(uint32_t address) override;
};
class FakeMemoryBackend final : public IMemoryBackend {
public:
    std::map<uint32_t, Bytes> regions;
    uint32_t failRead = 0, failWrite = 0;
    bool live = true;
    std::vector<std::pair<uint32_t, Bytes>> writes;
    Bytes read(uint32_t address, size_t size) override;
    void write(uint32_t address, const Bytes& data) override;
    bool alive() const override { return live; }
    DWORD protect(uint32_t, size_t, DWORD) override { return PAGE_READWRITE; }
    void flush(uint32_t, size_t) override {}
    uint32_t allocate(size_t size) override;
    void free(uint32_t address) override { regions.erase(address); }
};
class MemorySession {
public:
    std::shared_ptr<IMemoryBackend> backend;
    uint32_t moduleBase = 0x400000, moduleSize = 0x200000, referenceBase = 0x400000;
    DWORD pid = 0;
    bool readOnly = false;
    std::wstring imagePath;
    MemorySession(std::shared_ptr<IMemoryBackend> memory, uint32_t base, uint32_t size, DWORD process = 0) : backend(std::move(memory)), moduleBase(base), moduleSize(size), pid(process) {}
    uint32_t actual(uint32_t reference) const;
    Bytes read(uint32_t reference, size_t size) const;
    Bytes readActual(uint32_t address, size_t size) const;
    uint32_t u(uint32_t address, size_t width, bool actualAddress = false) const;
    void write(uint32_t address, const Bytes& bytes, bool actualAddress = false);
    void writeCode(uint32_t address, const Bytes& bytes, bool actualAddress = false);
    void writeU(uint32_t address, uint64_t value, size_t width, bool actualAddress = false);
    void writeTransaction(const std::vector<CompareWriteEntry>& entries);
    void compareExchangeTransaction(const std::vector<CompareWriteEntry>& entries);
    void guardedCompareExchangeTransaction(const std::vector<CompareWriteEntry>& entries);
    bool alive() const { return backend && backend->alive(); }
};
struct RemoteLease { Handle process, thread; uint32_t code = 0, data = 0; };
class RemoteCallExecutor {
    std::vector<RemoteLease> pending_;
    bool faulted_ = false;
public:
    ~RemoteCallExecutor();
    void reap();
    void run(MemorySession& session, const Bytes& code, const Bytes& data, DWORD timeout = 10000);
    bool faulted() const { return faulted_; }
    size_t pendingCount() const { return pending_.size(); }
};
}
