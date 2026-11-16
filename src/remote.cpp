#include "remote.hpp"
namespace banshou {
namespace {
const Bytes kRecalculateShell = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x08, 0xFF, 0x30, 0xFF,
    0x50, 0x04, 0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
const Bytes kReviveShell = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x8B, 0x45, 0x08, 0x8B, 0x08,
    0x8B, 0x50, 0x04, 0x89, 0x4D, 0xF0, 0x8B, 0x48, 0x08, 0x89, 0x55,
    0xF8, 0x8B, 0x50, 0x0C, 0x8B, 0x40, 0x10, 0x89, 0x4D, 0xF4, 0x89,
    0x55, 0xFC, 0x89, 0x45, 0x08, 0xFF, 0x75, 0xF0, 0xFF, 0x75, 0xF4,
    0xFF, 0x75, 0xF8, 0xFF, 0x75, 0xFC, 0xFF, 0x55, 0x08, 0x33, 0xC0,
    0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
const Bytes kDirection66Shell = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x6A, 0x01, 0x6A, 0x01, 0x6A,
    0x00, 0xFF, 0x30, 0x68, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x70, 0x04,
    0xFF, 0x50, 0x08, 0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
const Bytes kDirectionLegacyShell = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x6A, 0x00, 0xFF, 0x30, 0x68,
    0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x70, 0x04, 0x8B, 0x48, 0x08, 0xFF,
    0x50, 0x0C, 0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
const Bytes kPositionShell = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xFF, 0x30, 0xFF, 0x70, 0x04,
    0xFF, 0x70, 0x08, 0xFF, 0x70, 0x0C, 0x8B, 0x48, 0x10, 0xFF, 0x50,
    0x14, 0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
const Bytes kWeatherShell = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xFF, 0x30,
                             0xB9, 0,    0,    0,    0,    0xFF, 0x50, 0x04,
                             0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};

static void put(Bytes &b, size_t o, uint32_t v) { putNumber(b, o, 4, v); }
} // namespace

Bytes remoteShell(const std::string &key, bool is66) {
  if (key == "recalculate")
    return kRecalculateShell;
  if (key == "revive")
    return kReviveShell;
  if (key == "changeDirection")
    return is66 ? kDirection66Shell : kDirectionLegacyShell;
  if (key == "changePosition")
    return kPositionShell;
  if (key == "changeWeather")
    return kWeatherShell;
  throw Error("未知远程调用外壳: " + key);
}

void RemoteServices::recalculate(uint32_t code) {
  auto fn = ref("recalculate", 0x004075DF);
  Bytes data(8);
  put(data, 0, code);
  put(data, 4, fn);
  executor_.run(*session_, remoteShell("recalculate", profile_.is66()), data);
}
void RemoteServices::revive(uint32_t direction, uint32_t x, uint32_t y,
                            uint32_t code) {
  auto fn = ref("revive", 0x004092E0);
  Bytes data(20);
  put(data, 0, direction);
  put(data, 4, x);
  put(data, 8, y);
  put(data, 12, code);
  put(data, 16, fn);
  executor_.run(*session_, remoteShell("revive", profile_.is66()), data);
}
void RemoteServices::changeDirection(uint32_t direction, uint32_t code) {
  auto fn =
      ref("turn_refresh_direction", profile_.is66() ? 0x004575B8 : 0x00457428);
  if (profile_.is66()) {
    Bytes data(12);
    put(data, 0, direction);
    put(data, 4, code);
    put(data, 8, fn);
    executor_.run(*session_, remoteShell("changeDirection", true), data);
    return;
  }
  Bytes data(16);
  put(data, 0, direction);
  put(data, 4, code);
  put(data, 8, session_->actual(0x004B5DF0));
  put(data, 12, fn);
  executor_.run(*session_, remoteShell("changeDirection", false), data);
}
void RemoteServices::changePosition(uint32_t direction, uint32_t x, uint32_t y,
                                    uint32_t code) {
  // 6.6 的坐标刷新入口沿用旧版地址，调用约定从未实机验证（Python 版同样把
  // turn_refresh_position 标为不可用），这里直接拒绝，避免写到错误上下文。
  if (profile_.is66())
    throw Error("6.6 引擎没有经过验证的坐标刷新调用，已拒绝执行");
  auto fn = ref("turn_refresh_position", 0x004594DD);
  Bytes data(24);
  put(data, 0, direction);
  put(data, 4, y);
  put(data, 8, x);
  put(data, 12, code);
  put(data, 16, session_->actual(0x004B5DF0));
  put(data, 20, fn);
  executor_.run(*session_, remoteShell("changePosition", profile_.is66()), data);
}
void RemoteServices::changeWeather(uint32_t weather) {
  auto fn = ref("weather", 0x0041D9D1);
  auto shell = remoteShell("changeWeather", profile_.is66());
  put(shell, 9, session_->actual(0x004B3D08));
  Bytes data(8);
  put(data, 0, weather);
  put(data, 4, fn);
  executor_.run(*session_, shell, data);
}
} // namespace banshou
