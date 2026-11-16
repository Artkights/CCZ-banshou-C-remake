#include "memory.hpp"
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
namespace banshou {
ProcessInfo locateProcess(HWND window) {
    if (!window || !IsWindow(window)) throw Error("目标窗口已关闭或无效");
    ProcessInfo info; info.window = window;
    GetWindowThreadProcessId(window, &info.pid);
    if (!info.pid || info.pid == GetCurrentProcessId()) throw Error("请选择游戏窗口");
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, info.pid));
    if (!process) winError("OpenProcess(identity)");
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) winError("GetProcessTimes");
    info.created = (uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    info.path.resize(32768); DWORD length = DWORD(info.path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, info.path.data(), &length)) winError("QueryFullProcessImageNameW");
    info.path.resize(length);
    Handle snapshot;
    for (int attempt=0; attempt<4; ++attempt) {
        snapshot.reset(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32, info.pid));
        if (snapshot || GetLastError()!=ERROR_BAD_LENGTH) break;
    }
    if (!snapshot) winError("CreateToolhelp32Snapshot(modules)");
    MODULEENTRY32W module{}; module.dwSize=sizeof(module);
    if (!Module32FirstW(snapshot.get(), &module)) winError("Module32FirstW");
    info.base = uint32_t(reinterpret_cast<uintptr_t>(module.modBaseAddr)); info.size=module.modBaseSize;
    if (WaitForSingleObject(process.get(),0)!=WAIT_TIMEOUT || !IsWindow(window)) throw Error("游戏在附加期间退出");
    DWORD check{}; GetWindowThreadProcessId(window,&check);
    if (check!=info.pid) throw Error("目标窗口进程已变化");
    return info;
}
Win32MemoryBackend::Win32MemoryBackend(DWORD pid, bool readOnly) {
    DWORD access = PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE;
    if (!readOnly) access |= PROCESS_VM_WRITE|PROCESS_VM_OPERATION;
    handle_.reset(OpenProcess(access,FALSE,pid)); if (!handle_) winError("OpenProcess(memory)");
}
Bytes Win32MemoryBackend::read(uint32_t address, size_t size) {
    checkedAddress(address,0,size); Bytes bytes(size); SIZE_T done{};
    if (!ReadProcessMemory(handle_.get(),reinterpret_cast<LPCVOID>(uintptr_t(address)),bytes.data(),size,&done) || done!=size) winError("ReadProcessMemory",address,size);
    return bytes;
}
void Win32MemoryBackend::write(uint32_t address,const Bytes& bytes) {
    checkedAddress(address,0,bytes.size()); SIZE_T done{};
    if (!WriteProcessMemory(handle_.get(),reinterpret_cast<LPVOID>(uintptr_t(address)),bytes.data(),bytes.size(),&done) || done!=bytes.size()) winError("WriteProcessMemory",address,bytes.size());
}
bool Win32MemoryBackend::alive() const { return WaitForSingleObject(handle_.get(),0)==WAIT_TIMEOUT; }
DWORD Win32MemoryBackend::protect(uint32_t address,size_t size,DWORD protection) {
    DWORD before{};
    if (!VirtualProtectEx(handle_.get(),reinterpret_cast<LPVOID>(uintptr_t(address)),size,protection,&before)) winError("VirtualProtectEx",address,size);
    return before;
}
void Win32MemoryBackend::flush(uint32_t address,size_t size) {
    if (!FlushInstructionCache(handle_.get(),reinterpret_cast<LPCVOID>(uintptr_t(address)),size)) winError("FlushInstructionCache",address,size);
}
uint32_t Win32MemoryBackend::allocate(size_t size) {
    auto address=VirtualAllocEx(handle_.get(),nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if (!address) winError("VirtualAllocEx"); return uint32_t(reinterpret_cast<uintptr_t>(address));
}
void Win32MemoryBackend::free(uint32_t address) {
    if (!VirtualFreeEx(handle_.get(),reinterpret_cast<LPVOID>(uintptr_t(address)),0,MEM_RELEASE)) winError("VirtualFreeEx",address);
}
Bytes FakeMemoryBackend::read(uint32_t address,size_t size) {
    checkedAddress(address,0,size);
    if (!live || (failRead && address<=failRead && uint64_t(address)+size>failRead)) throw Error("Injected read failure");
    auto it=regions.upper_bound(address); if (it==regions.begin()) throw Error("Unmapped read"); --it;
    size_t offset=address-it->first;
    if (offset>it->second.size() || size>it->second.size()-offset) throw Error("Short read");
    return Bytes(it->second.begin()+offset,it->second.begin()+offset+size);
}
void FakeMemoryBackend::write(uint32_t address,const Bytes& bytes) {
    read(address,bytes.size());
    if (failWrite && address<=failWrite && uint64_t(address)+bytes.size()>failWrite) throw Error("Injected write failure");
    auto it=regions.upper_bound(address); --it;
    std::copy(bytes.begin(),bytes.end(),it->second.begin()+(address-it->first)); writes.emplace_back(address,bytes);
}
uint32_t FakeMemoryBackend::allocate(size_t size) {
    uint32_t address=regions.empty()?0x10000000:checkedAddress(regions.rbegin()->first,regions.rbegin()->second.size()+0x1000,size);
    regions[address]=Bytes(size); return address;
}
uint32_t MemorySession::actual(uint32_t reference) const {
    auto value=int64_t(reference)+int64_t(moduleBase)-referenceBase;
    if (reference<referenceBase || value<=0 || value>UINT32_MAX) throw Error("Invalid reference VA");
    return uint32_t(value);
}
Bytes MemorySession::read(uint32_t reference,size_t size) const { return readActual(actual(reference),size); }
Bytes MemorySession::readActual(uint32_t address,size_t size) const { if (!alive()) throw Error("游戏进程已退出"); return backend->read(address,size); }
uint32_t MemorySession::u(uint32_t address,size_t width,bool actualAddress) const { return uint32_t(number(actualAddress?readActual(address,width):read(address,width),0,width)); }
void MemorySession::write(uint32_t address,const Bytes& bytes,bool actualAddress) {
    if (readOnly) throw Error("只读会话不能写入");
    if (!alive()) throw Error("游戏进程已退出");
    auto target=actualAddress?address:actual(address);
    try { backend->write(target,bytes); }
    catch (const Error& e) { if (e.code!=ERROR_ACCESS_DENIED && e.code!=ERROR_NOACCESS) throw; writeCode(target,bytes,true); }
}
void MemorySession::writeCode(uint32_t address,const Bytes& bytes,bool actualAddress) {
    if (readOnly || !alive()) throw Error("会话不能写入代码");
    auto target=actualAddress?address:actual(address); checkedAddress(target,0,bytes.size());
    std::vector<std::pair<uint32_t,DWORD>> pages;
    std::exception_ptr failure;
    try {
        uint64_t end=uint64_t(target)+bytes.size();
        for (uint64_t page=target&~uint32_t(4095);page<end;page+=4096) pages.emplace_back(uint32_t(page),backend->protect(uint32_t(page),4096,PAGE_EXECUTE_READWRITE));
        backend->write(target,bytes); backend->flush(target,bytes.size());
    } catch (...) { failure=std::current_exception(); }
    for (auto it=pages.rbegin();it!=pages.rend();++it) try { backend->protect(it->first,4096,it->second); } catch (...) { log("Code page protection restore failed"); if(!failure) failure=std::current_exception(); }
    if(failure) std::rethrow_exception(failure);
}
void MemorySession::writeU(uint32_t address,uint64_t value,size_t width,bool actualAddress) { Bytes b(width);putNumber(b,0,width,value);write(address,b,actualAddress); }
void MemorySession::writeTransaction(const std::vector<CompareWriteEntry>& entries) {
    if (readOnly) throw Error("只读会话不能写入");
    if (!alive()) throw Error("游戏进程已退出");
    struct Applied { CompareWriteEntry entry; Bytes original; };
    std::vector<Applied> applied;
    try {
        for (const auto &entry : entries) {
            if (entry.replacement.empty() || entry.expected.size() != entry.replacement.size())
                throw Error("事务写入条目长度无效");
            auto original = entry.actualAddress ? readActual(entry.address, entry.replacement.size())
                                                : read(entry.address, entry.replacement.size());
            applied.push_back({entry, original});
            if (entry.code) writeCode(entry.address, entry.replacement, entry.actualAddress);
            else write(entry.address, entry.replacement, entry.actualAddress);
        }
    } catch (...) {
        for (auto it = applied.rbegin(); it != applied.rend(); ++it)
            try { if (it->entry.code) writeCode(it->entry.address, it->original, it->entry.actualAddress);
                  else write(it->entry.address, it->original, it->entry.actualAddress); }
            catch (...) { log("事务回滚失败"); }
        throw;
    }
}
void MemorySession::compareExchangeTransaction(const std::vector<CompareWriteEntry>& entries) {
    if (readOnly) throw Error("只读会话不能写入");
    if (!alive()) throw Error("游戏进程已退出");
    struct Original { CompareWriteEntry entry; Bytes bytes; };
    std::vector<Original> originals;
    originals.reserve(entries.size());
    for (const auto &entry : entries) {
        if (entry.replacement.empty() || entry.expected.size() != entry.replacement.size())
            throw Error("事务写入条目长度无效");
        auto current = entry.actualAddress ? readActual(entry.address, entry.expected.size())
                                           : read(entry.address, entry.expected.size());
        if (current != entry.expected)
            throw Error("原字节已变化，拒绝写入");
        originals.push_back({entry, current});
    }
    auto restore = [&] {
        for (auto it = originals.rbegin(); it != originals.rend(); ++it)
            try { if (it->entry.code) writeCode(it->entry.address, it->bytes, it->entry.actualAddress);
                  else write(it->entry.address, it->bytes, it->entry.actualAddress); }
            catch (...) { log("事务回滚失败"); }
    };
    try {
        for (const auto &entry : entries) {
            if (entry.code) writeCode(entry.address, entry.replacement, entry.actualAddress);
            else write(entry.address, entry.replacement, entry.actualAddress);
        }
        for (const auto &entry : entries) {
            auto current = entry.actualAddress ? readActual(entry.address, entry.replacement.size())
                                               : read(entry.address, entry.replacement.size());
            if (current != entry.replacement)
                throw Error("写入后复读失败");
        }
    } catch (...) {
        restore();
        throw;
    }
}
void MemorySession::guardedCompareExchangeTransaction(const std::vector<CompareWriteEntry>& entries) {
    compareExchangeTransaction(entries);
}
static bool freeRemote(RemoteLease& lease) {
    bool ok=true;
    if(lease.code && VirtualFreeEx(lease.process.get(),reinterpret_cast<void*>(uintptr_t(lease.code)),0,MEM_RELEASE)) lease.code=0; else if(lease.code) ok=false;
    if(lease.data && VirtualFreeEx(lease.process.get(),reinterpret_cast<void*>(uintptr_t(lease.data)),0,MEM_RELEASE)) lease.data=0; else if(lease.data) ok=false;
    if(!ok) log("远程分配释放失败，保留到目标进程退出");
    return ok;
}
RemoteCallExecutor::~RemoteCallExecutor() { reap(); if(!pending_.empty()) log("Unfinished remote allocations retained in target until thread/process exit"); }
void RemoteCallExecutor::reap() {
    for(auto it=pending_.begin();it!=pending_.end();) {
        if(WaitForSingleObject(it->thread.get(),0)==WAIT_OBJECT_0 || WaitForSingleObject(it->process.get(),0)==WAIT_OBJECT_0) { if(freeRemote(*it)) it=pending_.erase(it); else ++it; } else ++it;
    }
}
void RemoteCallExecutor::run(MemorySession& session,const Bytes& code,const Bytes& data,DWORD timeout) {
    reap();
    if(faulted_ || !session.alive()) throw Error("远程调用会话不可用，请重新附加");
    RemoteLease lease;
    lease.process.reset(OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|SYNCHRONIZE,FALSE,session.pid));
    if(!lease.process) winError("OpenProcess(remote)");
    try {
        auto allocate=[&](const Bytes& bytes) {
            auto p=VirtualAllocEx(lease.process.get(),nullptr,std::max<size_t>(1,bytes.size()),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) winError("VirtualAllocEx(remote)");
            SIZE_T written{};
            if(!WriteProcessMemory(lease.process.get(),p,bytes.data(),bytes.size(),&written) || written!=bytes.size()) {VirtualFreeEx(lease.process.get(),p,0,MEM_RELEASE);winError("WriteProcessMemory(remote)");}
            Bytes verification(bytes.size()); SIZE_T read{}; if(!ReadProcessMemory(lease.process.get(),p,verification.data(),verification.size(),&read)||read!=verification.size()||verification!=bytes){VirtualFreeEx(lease.process.get(),p,0,MEM_RELEASE);throw Error("远程参数或代码写后复读失败");}
            return uint32_t(reinterpret_cast<uintptr_t>(p));
        };
        lease.code=allocate(code);lease.data=allocate(data);
        DWORD before{};
        if(!VirtualProtectEx(lease.process.get(),reinterpret_cast<void*>(uintptr_t(lease.code)),code.size(),PAGE_EXECUTE_READ,&before) || !FlushInstructionCache(lease.process.get(),reinterpret_cast<void*>(uintptr_t(lease.code)),code.size())) winError("Protect/flush remote code");
        lease.thread.reset(CreateRemoteThread(lease.process.get(),nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(uintptr_t(lease.code)),reinterpret_cast<void*>(uintptr_t(lease.data)),0,nullptr));
        if(!lease.thread) winError("CreateRemoteThread");
        auto wait=WaitForSingleObject(lease.thread.get(),timeout);
        if(wait!=WAIT_OBJECT_0) {pending_.push_back(std::move(lease));faulted_=true;throw Error("远程调用超时或等待失败；执行中的代码已保留，不再发送远程调用");}
        DWORD result{};GetExitCodeThread(lease.thread.get(),&result);
        if(result>=0xC0000000u) throw Error("远程线程异常退出");
        if(!freeRemote(lease)) { pending_.push_back(std::move(lease)); faulted_=true; }
    } catch (...) { if(lease.process && (!lease.thread || WaitForSingleObject(lease.thread.get(),0)==WAIT_OBJECT_0)) freeRemote(lease);throw; }
}
}
