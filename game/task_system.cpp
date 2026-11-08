// ============================================================================
// task_system.cpp
// ============================================================================
#include "task_system.hpp"

#include <cstdio>
#include <stdexcept>

namespace ch {

Task& TaskManager::link(std::unique_ptr<Task> task, std::uint32_t priority, std::uint32_t param) {
    if (!task) throw std::invalid_argument("TaskManager::link: null task");
    Task& ref = *task;
    insert(ref, std::move(task), priority, param);
    return ref;
}

void TaskManager::link(Task& task, std::uint32_t priority, std::uint32_t param) {
    insert(task, nullptr, priority, param);
}

// FUN_004216c0 + FUN_00421c70: append to the tail of the priority list.
void TaskManager::insert(Task& t, std::unique_ptr<Task> owned, std::uint32_t priority, std::uint32_t param) {
    if (priority >= kPriorityLevels) throw std::out_of_range("TaskManager::link: priority >= 32");
    if (t.isLinked()) {
        std::fprintf(stderr, "ERROR: already link task (%p)\n", static_cast<void*>(&t));
        return;
    }
    t.state_         = Task::State::Linked;
    t.waitFrames_    = 0;
    t.param_         = param;
    t.killRequested_ = false;
    lists_[priority].push_back(Entry{&t, std::move(owned)});
}

// FUN_004217c0. `next` is captured before running a task, as the original
// did, so a task appended behind the last element waits until next frame.
void TaskManager::runFrame() {
    for (auto& list : lists_) {
        for (auto it = list.begin(); it != list.end();) {
            auto next = std::next(it);
            exec(list, it);
            it = next;
        }
    }
}

// FUN_00421850
void TaskManager::exec(List& list, List::iterator it) {
    Task& t = *it->task;

    if (t.state_ == Task::State::Linked) {
        t.root_.emplace(t.start(t.param_));          // thread create + first resume
        t.resumePoint_ = t.root_->h_;
        t.state_       = Task::State::Running;
    } else if (t.waitFrames_ != 0) {
        --t.waitFrames_;
        return;
    }

    Task* const outer = Task::current_;
    Task::current_ = &t;
    std::exchange(t.resumePoint_, {}).resume();
    Task::current_ = outer;

    const auto& rootPromise = t.root_->h_.promise();
    const bool  finished    = t.killRequested_ || t.root_->h_.done();
    if (!finished && !t.resumePoint_) {
        // Suspended on an awaiter that is not ours: it can never be resumed.
        std::fprintf(stderr, "ERROR: task %p suspended outside yieldFrame()\n", static_cast<void*>(&t));
    }
    if (!finished) return;

    std::exception_ptr error = rootPromise.error;
    retire(t);
    list.erase(it);                                  // deletes owned tasks (+0x28)
    if (error) std::rethrow_exception(error);
}

void TaskManager::retire(Task& t) noexcept {
    t.root_.reset();                                 // destroys the whole frame chain
    t.resumePoint_ = {};
    t.state_       = Task::State::Unlinked;
}

// FUN_00421bf0 (TerminateThread on every task).
void TaskManager::killAll() {
    for (auto& list : lists_) {
        while (!list.empty()) {
            retire(*list.front().task);
            list.pop_front();
        }
    }
}

std::optional<std::uint32_t> TaskManager::priorityOf(const Task& t) const noexcept {
    for (std::uint32_t p = 0; p < kPriorityLevels; ++p)
        for (const auto& e : lists_[p])
            if (e.task == &t) return p;
    return std::nullopt;
}

bool TaskManager::empty() const noexcept {
    for (const auto& l : lists_) if (!l.empty()) return false;
    return true;
}

} // namespace ch
