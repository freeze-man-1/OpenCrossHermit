// ============================================================================
// task_system.hpp — cooperative task scheduler (replaces src/system/m*Task*)
//
// The original ran every task on its own Win32 thread and hand-shook with the
// scheduler through two events per frame, so exactly one thread ever ran at a
// time. That is a coroutine system built out of threads; here it is rebuilt on
// C++20 stackless coroutines, which run on the browser main thread without
// pthreads or Asyncify.
//
//   FUN_004216c0  TaskLink(task, prio, param)   -> TaskManager::link
//   FUN_004217c0  TaskExec (32 priority lists)  -> TaskManager::runFrame
//   FUN_00421850  TaskExecSub                   -> TaskManager::exec
//   FUN_00422360  TaskWait(skipFrames)          -> co_await ch::yieldFrame(n)
//   FUN_00422440  TaskKill (self)               -> co_await ch::killSelf()
//   FUN_00421bf0  TaskKillAll                   -> TaskManager::killAll
//   FUN_00421e20  TaskFindPriority              -> TaskManager::priorityOf
// ============================================================================
#pragma once

#include <array>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <list>
#include <memory>
#include <optional>
#include <utility>

namespace ch {

// ----------------------------------------------------------- Co<T> --------
// Awaitable coroutine. A sub-routine that waits for frames is written as
// `Co<int> menu() { ...; co_await yieldFrame(); ... co_return sel; }` and
// called with `int sel = co_await menu();` — same shape as the original code.
template <class T = void> class Co;

namespace detail {
struct PromiseBase {
    std::coroutine_handle<> continuation{};
    std::exception_ptr      error{};

    std::suspend_always initial_suspend() const noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }
        template <class P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) const noexcept {
            auto next = h.promise().continuation;
            return next ? next : std::noop_coroutine();
        }
        void await_resume() const noexcept {}
    };
    FinalAwaiter final_suspend() const noexcept { return {}; }
    void unhandled_exception() noexcept { error = std::current_exception(); }
};
} // namespace detail

template <class T>
class [[nodiscard]] Co {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type : detail::PromiseBase {
        std::optional<T> value;
        Co get_return_object() noexcept { return Co{Handle::from_promise(*this)}; }
        template <class U> void return_value(U&& v) { value.emplace(std::forward<U>(v)); }
    };

    Co(Co&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    Co& operator=(Co&& o) noexcept { if (this != &o) { reset(); h_ = std::exchange(o.h_, {}); } return *this; }
    Co(const Co&) = delete;
    Co& operator=(const Co&) = delete;
    ~Co() { reset(); }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        h_.promise().continuation = caller;
        return h_;
    }
    T await_resume() {
        auto& p = h_.promise();
        if (p.error) std::rethrow_exception(p.error);
        return std::move(*p.value);
    }

private:
    explicit Co(Handle h) noexcept : h_(h) {}
    void reset() noexcept { if (h_) h_.destroy(); h_ = {}; }
    Handle h_{};
};

template <>
class [[nodiscard]] Co<void> {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type : detail::PromiseBase {
        Co get_return_object() noexcept { return Co{Handle::from_promise(*this)}; }
        void return_void() const noexcept {}
    };

    Co(Co&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    Co& operator=(Co&& o) noexcept { if (this != &o) { reset(); h_ = std::exchange(o.h_, {}); } return *this; }
    Co(const Co&) = delete;
    Co& operator=(const Co&) = delete;
    ~Co() { reset(); }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        h_.promise().continuation = caller;
        return h_;
    }
    void await_resume() {
        if (h_.promise().error) std::rethrow_exception(h_.promise().error);
    }

private:
    friend class TaskManager;
    explicit Co(Handle h) noexcept : h_(h) {}
    void reset() noexcept { if (h_) h_.destroy(); h_ = {}; }
    Handle h_{};
};

// ------------------------------------------------------------ Task --------
class Task {
public:
    Task() = default;
    virtual ~Task() = default;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    // Original vtable slot 0: thread body, receives the param given at link time.
    virtual Co<> start(std::uint32_t param) = 0;

    [[nodiscard]] bool isLinked() const noexcept { return state_ != State::Unlinked; }

private:
    friend class TaskManager;
    friend struct YieldAwaiter;
    friend struct KillAwaiter;

    enum class State : std::uint8_t { Unlinked, Linked, Running };

    inline static Task* current_ = nullptr;   // task whose coroutine is executing

    State                   state_ = State::Unlinked;
    std::uint32_t           waitFrames_ = 0;  // +0x14: frames still to skip
    std::uint32_t           param_ = 0;       // +0x18: start() argument
    bool                    killRequested_ = false;
    std::optional<Co<>>     root_;
    std::coroutine_handle<> resumePoint_{};   // innermost suspended frame
};

// FUN_00422360: give the frame back to the scheduler. The task resumes on the
// next frame after skipping `skipFrames` additional frames.
struct YieldAwaiter {
    std::uint32_t skipFrames;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const noexcept {
        Task::current_->waitFrames_  = skipFrames;
        Task::current_->resumePoint_ = h;
    }
    void await_resume() const noexcept {}
};
[[nodiscard]] inline YieldAwaiter yieldFrame(std::uint32_t skipFrames = 0) noexcept { return {skipFrames}; }

// FUN_00422440: terminate the calling task from any call depth.
struct KillAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept { Task::current_->killRequested_ = true; }
    void await_resume() const noexcept {}
};
[[nodiscard]] inline KillAwaiter killSelf() noexcept { return {}; }

// ----------------------------------------------------- TaskManager --------
class TaskManager {
public:
    static constexpr std::size_t kPriorityLevels = 32;   // 0x20 lists, 0 runs first

    TaskManager() = default;
    ~TaskManager() { killAll(); }
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;

    // Task byte +0x28 == 1 in the original: manager deletes the task when it ends.
    Task& link(std::unique_ptr<Task> task, std::uint32_t priority, std::uint32_t param = 0);
    // +0x28 == 0: caller keeps ownership; must outlive its time in the list.
    void  link(Task& task, std::uint32_t priority, std::uint32_t param = 0);

    void  runFrame();
    void  killAll();

    [[nodiscard]] std::optional<std::uint32_t> priorityOf(const Task& t) const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    struct Entry {
        Task*                 task;
        std::unique_ptr<Task> owned;
    };
    using List = std::list<Entry>;

    void insert(Task& t, std::unique_ptr<Task> owned, std::uint32_t priority, std::uint32_t param);
    void exec(List& list, List::iterator it);
    static void retire(Task& t) noexcept;

    std::array<List, kPriorityLevels> lists_;
};

} // namespace ch
