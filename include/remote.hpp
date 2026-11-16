#pragma once
#include "memory.hpp"
#include "profile.hpp"
namespace banshou {
Bytes remoteShell(const std::string &key, bool is66);
class RemoteServices {
    std::shared_ptr<MemorySession> session_; EngineProfile profile_; RemoteCallExecutor executor_;
    uint32_t ref(const char* key,uint32_t fallback)const{return session_->actual(profile_.remoteAddress(key,fallback));}
public:
    RemoteServices(std::shared_ptr<MemorySession>s,EngineProfile p):session_(std::move(s)),profile_(std::move(p)){}
    void recalculate(uint32_t code); void revive(uint32_t direction,uint32_t x,uint32_t y,uint32_t code); void changeDirection(uint32_t direction,uint32_t code); void changePosition(uint32_t direction,uint32_t x,uint32_t y,uint32_t code); void changeWeather(uint32_t weather);
    bool faulted()const{return executor_.faulted();}
};
}
