#include "session.hpp"
#include <exception>
#include <thread>
namespace banshou {
SessionController::SessionController() {
  worker_ = std::thread([this] { loop(); });
}
SessionController::~SessionController() {
  {
    std::lock_guard<std::mutex> l(mutex_);
    stop_ = true;
    cv_.notify_all();
  }
  if (worker_.joinable())
    worker_.join();
}
bool SessionController::waitFor(SessionState expected, DWORD timeout) const {
  std::unique_lock<std::mutex> l(mutex_);
  return cv_.wait_for(l, std::chrono::milliseconds(timeout),
                      [this, expected] { return state_ == expected; });
}
void SessionController::loop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> l(mutex_);
      cv_.wait(l, [this] { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty())
        return;
      job = std::move(queue_.front());
      queue_.pop_front();
    }
    try {
      job();
    } catch (const std::exception &e) {
      log(e.what());
    }
  }
}
uint64_t SessionController::attach(HWND window, int versionHint,
                                   bool readOnly) {
  std::lock_guard<std::mutex> l(mutex_);
  if (!window || !IsWindow(window))
    throw Error("目标窗口已关闭或无效");
  if (state_ == SessionState::Attached && process_.window == window &&
      session_ && session_->alive() && session_->readOnly == readOnly)
    return generation_;
  auto gen = ++generation_;
  if (state_ == SessionState::Attaching || state_ == SessionState::Detaching)
    queue_.clear();
  state_ = SessionState::Attaching;
  cv_.notify_all();
  if (onState)
    onState(state_, gen, "");
  queue_.push_back([this, window, versionHint, gen, readOnly] {
    try {
      auto pi = locateProcess(window);
      auto mem = std::make_shared<Win32MemoryBackend>(pi.pid, readOnly);
      auto s = std::make_shared<MemorySession>(mem, pi.base, pi.size, pi.pid);
      s->imagePath = pi.path;
      s->readOnly = readOnly;
      ProfileRegistry registry;
      // 加壳或延迟初始化的引擎在刚启动时可能取不到完整画像：退避重试最多
      // 30 秒（60 次 × 500ms），期间可通过 detach 取消。
      EngineProfile p;
      std::exception_ptr lastError;
      for (int attempt = 0; attempt < 60; ++attempt) {
        {
          std::lock_guard<std::mutex> l(mutex_);
          if (gen != generation_)
            return;
        }
        try {
          p = registry.detect(pi.path, versionHint);
          lastError = nullptr;
          break;
        } catch (const std::exception &e) {
          lastError = std::current_exception();
          log(std::string("画像探测未就绪，500ms 后重试：") + e.what());
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
      }
      if (lastError)
        std::rethrow_exception(lastError);
      try {
        RuntimeModuleImage runtime(pi.path, s);
        registry.enrichLegacyDirect(p, runtime);
      } catch (const std::exception &e) {
        log(std::string("运行时画像补充失败：") + e.what());
      }
      {
        std::lock_guard<std::mutex> l(mutex_);
        if (gen != generation_)
          return;
        process_ = pi;
        profile_ = p;
        session_ = s;
        state_ = SessionState::Attached;
        cv_.notify_all();
      }
      if (onState)
        onState(SessionState::Attached, gen, "");
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> l(mutex_);
      if (gen == generation_) {
        state_ = SessionState::Error;
        session_.reset();
        cv_.notify_all();
      }
      if (onState)
        onState(SessionState::Error, gen, e.what());
    }
  });
  cv_.notify_one();
  return gen;
}
void SessionController::detach() {
  std::lock_guard<std::mutex> l(mutex_);
  if (state_ == SessionState::Detached || state_ == SessionState::Detaching)
    return;
  ++generation_;
  if (state_ == SessionState::Attaching)
    queue_.clear();
  state_ = SessionState::Detaching;
  auto gen = generation_;
  cv_.notify_all();
  if (onState)
    onState(state_, gen, "");
  queue_.push_back([this, gen] {
    std::lock_guard<std::mutex> l(mutex_);
    if (gen != generation_)
      return;
    session_.reset();
    profile_ = EngineProfile{};
    process_ = ProcessInfo{};
    state_ = SessionState::Detached;
    cv_.notify_all();
    if (onState)
      onState(state_, gen, "");
  });
  cv_.notify_one();
}
} // namespace banshou
