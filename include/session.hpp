#pragma once
#include "game.hpp"
#include <condition_variable>
#include <deque>
#include <chrono>
namespace banshou {
enum class SessionState { Detached, Attaching, Attached, Detaching, Error };
class SessionController {
    mutable std::mutex mutex_; mutable std::condition_variable cv_; std::deque<std::function<void()>> queue_; std::thread worker_; bool stop_=false; uint64_t generation_=0; SessionState state_=SessionState::Detached; std::shared_ptr<MemorySession> session_; EngineProfile profile_; ProcessInfo process_;
    void loop();
public:
    std::function<void(SessionState,uint64_t,const std::string&)> onState;
    SessionController(); ~SessionController();
    uint64_t attach(HWND window,int versionHint=-1,bool readOnly=false); void detach();
    SessionState state()const{std::lock_guard<std::mutex>l(mutex_);return state_;} uint64_t generation()const{std::lock_guard<std::mutex>l(mutex_);return generation_;}
    bool waitFor(SessionState expected,DWORD timeout=2000) const;
    std::shared_ptr<MemorySession> session()const{std::lock_guard<std::mutex>l(mutex_);return session_;} EngineProfile profile()const{std::lock_guard<std::mutex>l(mutex_);return profile_;} ProcessInfo process()const{std::lock_guard<std::mutex>l(mutex_);return process_;}
};
}
