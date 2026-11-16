#include "platform.hpp"
#include <bcrypt.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
namespace banshou {
std::wstring wide(const std::string& text, UINT cp) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) throw Error("Text too large");
    auto length = MultiByteToWideChar(cp, 0, text.data(), int(text.size()), nullptr, 0);
    if (!length) winError("MultiByteToWideChar");
    std::wstring result(length, L'\0');
    MultiByteToWideChar(cp, 0, text.data(), int(text.size()), result.data(), length);
    return result;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    auto length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) winError("WideCharToMultiByte");
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}
std::wstring fullPath(const std::wstring& path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    auto n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!n) winError("GetFullPathNameW");
    std::wstring result(n + 1, L'\0');
    auto used = GetFullPathNameW(path.c_str(), DWORD(result.size()), result.data(), nullptr);
    if (!used || used >= result.size()) winError("GetFullPathNameW");
    result.resize(used);
    if (result.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + result.substr(2);
    return L"\\\\?\\" + result;
}
std::wstring parentPath(const std::wstring& path) { return path.substr(0, path.find_last_of(L"\\/")); }
std::wstring exePath() {
    std::wstring path(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!n || n >= path.size()) winError("GetModuleFileNameW");
    path.resize(n); return path;
}
Bytes readFile(const std::wstring& path) {
    Handle file(CreateFileW(fullPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) winError("CreateFileW(read)");
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length)) winError("GetFileSizeEx");
    if (length.QuadPart < 0 || length.QuadPart > 512ll * 1024 * 1024) throw Error("File exceeds 512 MiB bound");
    Bytes data(size_t(length.QuadPart));
    DWORD done = 0;
    if (!data.empty() && (!ReadFile(file.get(), data.data(), DWORD(data.size()), &done, nullptr) || done != data.size())) winError("ReadFile");
    return data;
}
void writeFile(const std::wstring& path, const Bytes& data) {
    Handle file(CreateFileW(fullPath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) winError("CreateFileW(write)");
    DWORD done = 0;
    if (!data.empty() && (!WriteFile(file.get(), data.data(), DWORD(data.size()), &done, nullptr) || done != data.size())) winError("WriteFile");
}
void writeText(const std::wstring& path, const std::string& text) { writeFile(path, Bytes(text.begin(), text.end())); }
std::string hex(const Bytes& data) {
    static const char digits[] = "0123456789ABCDEF";
    std::string result; result.reserve(data.size() * 2);
    for (auto c : data) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
Bytes unhex(const std::string& text) {
    Bytes result; int high = -1;
    for (unsigned char c : text) {
        if (isspace(c)) continue;
        int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (n < 0) throw Error("Invalid hex bytes");
        if (high < 0) high = n; else { result.push_back(uint8_t((high << 4) | n)); high = -1; }
    }
    if (high >= 0) throw Error("Odd hex byte count");
    return result;
}
std::string sha256(const Bytes& data) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hashHandle{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw Error("SHA256 provider failed");
    Bytes digest(32);
    auto status = BCryptCreateHash(algorithm, &hashHandle, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hashHandle, const_cast<PUCHAR>(data.data()), ULONG(data.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hashHandle, digest.data(), ULONG(digest.size()), 0);
    if (hashHandle) BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw Error("SHA256 failed");
    return hex(digest);
}
uint64_t number(const Bytes& data, size_t offset, size_t width) {
    if (!width || width > 8 || offset > data.size() || width > data.size() - offset) throw Error("Truncated numeric field");
    uint64_t result = 0; for (size_t i = 0; i < width; ++i) result |= uint64_t(data[offset+i]) << (8*i);
    return result;
}
void putNumber(Bytes& data, size_t offset, size_t width, uint64_t value) {
    if (!width || width > 8 || offset > data.size() || width > data.size()-offset || (width < 8 && value >= (uint64_t(1) << (width*8)))) throw Error("Numeric field out of range");
    for (size_t i = 0; i < width; ++i) data[offset+i] = uint8_t(value >> (8*i));
}
uint32_t checkedAddress(uint32_t base, uint64_t offset, uint64_t size) {
    if (!base || offset > UINT32_MAX || uint64_t(base)+offset > UINT32_MAX || !size || size > uint64_t(UINT32_MAX)+1-(uint64_t(base)+offset)) throw Error("Invalid or overflowing remote address");
    return uint32_t(uint64_t(base)+offset);
}
Json resourceJson(int id) {
    auto module = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) winError("FindResourceW");
    auto loaded=LoadResource(module, resource); auto data = static_cast<const char*>(loaded?LockResource(loaded):nullptr); auto size=SizeofResource(module, resource);
    if(!data||!size) winError("LoadResource");
    return Json::parse(data, data+size);
}
void winError(const char* operation, uint32_t address, size_t size) {
    auto error = GetLastError();
    std::ostringstream out; out << operation << " failed (WinError=" << error << ", address=0x" << std::hex << address << ", size=" << size << ')';
    throw Error(out.str(), error);
}
static std::wstring logDirectory() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) return {};
    std::wstring directory = std::wstring(path) + L"\\CaoCaoToolbox\\logs";
    CoTaskMemFree(path);
    std::filesystem::create_directories(directory);
    return directory;
}
void log(const std::string& message) noexcept {
    static std::mutex lock;
    try {
        std::lock_guard<std::mutex> guard(lock);
        auto directory = logDirectory(); if (directory.empty()) return;
        auto path = directory + L"\\native.log";
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 2*1024*1024) MoveFileExW(path.c_str(), (path+L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
        Handle file(CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) return;
        SYSTEMTIME now{}; GetLocalTime(&now);
        std::ostringstream line; line << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' ' << now.wHour << ':' << now.wMinute << ':' << now.wSecond << ' ' << message << "\r\n";
        auto text = line.str(); DWORD done{}; WriteFile(file.get(), text.data(), DWORD(text.size()), &done, nullptr);
    } catch (...) {}
}
static LONG WINAPI crash(EXCEPTION_POINTERS* info) {
    try {
        auto path = logDirectory()+L"\\native-"+std::to_wstring(GetCurrentProcessId())+L".dmp";
        Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        MINIDUMP_EXCEPTION_INFORMATION data{GetCurrentThreadId(), info, FALSE};
        if (file) MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file.get(), MiniDumpNormal, &data, nullptr, nullptr);
    } catch (...) {}
    return EXCEPTION_EXECUTE_HANDLER;
}
void installDiagnostics() { SetUnhandledExceptionFilter(crash); }
}
