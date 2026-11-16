#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <json.hpp>
namespace banshou {
using Bytes = std::vector<uint8_t>;
using Json = nlohmann::ordered_json;
class Error : public std::runtime_error {
public:
    DWORD code;
    Error(std::string message, DWORD error = 0) : std::runtime_error(std::move(message)), code(error) {}
};
class Handle {
    HANDLE value_ = nullptr;
public:
    Handle() = default;
    explicit Handle(HANDLE v) : value_(v) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept { if (this != &other) reset(other.release()); return *this; }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE release() { auto r = value_; value_ = nullptr; return r; }
    void reset(HANDLE v = nullptr) { if (*this) CloseHandle(value_); value_ = v; }
};
std::wstring wide(const std::string& text, UINT cp = CP_UTF8);
std::string utf8(const std::wstring& text);
std::wstring fullPath(const std::wstring& path);
std::wstring parentPath(const std::wstring& path);
std::wstring exePath();
Bytes readFile(const std::wstring& path);
void writeFile(const std::wstring& path, const Bytes& data);
void writeText(const std::wstring& path, const std::string& text);
std::string sha256(const Bytes& data);
std::string hex(const Bytes& data);
Bytes unhex(const std::string& text);
uint64_t number(const Bytes& data, size_t offset, size_t width);
void putNumber(Bytes& data, size_t offset, size_t width, uint64_t value);
uint32_t checkedAddress(uint32_t base, uint64_t offset, uint64_t size = 1);
Json resourceJson(int id);
[[noreturn]] void winError(const char* operation, uint32_t address = 0, size_t size = 0);
void log(const std::string& message) noexcept;
void installDiagnostics();
}
