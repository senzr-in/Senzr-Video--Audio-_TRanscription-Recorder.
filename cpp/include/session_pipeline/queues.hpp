#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>
#include <chrono>
#include <cstddef>

namespace session_pipeline {

// ─────────────────────────────────────────────────────────────────────────────
// ThreadSafeQueue<T>
//
// Bounded, thread-safe queue supporting:
//   - Multiple producers + multiple consumers
//   - Blocking pop with timeout
//   - Non-blocking try_push (drop-oldest when full — matches Python put_nowait)
//   - Clean shutdown without deadlock
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(std::size_t max_size)
        : max_size_(max_size) {}

    // ── Non-blocking push ─────────────────────────────────────────────────────
    // If queue is full: drops oldest item then inserts new one.
    // Matches Python: try get_nowait() then put_nowait().
    // Returns true if pushed without dropping, false if oldest was dropped.
    bool try_push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool dropped = false;
        if (queue_.size() >= max_size_) {
            queue_.pop();      // drop oldest
            dropped = true;
        }
        queue_.push(std::move(item));
        lock.unlock();
        not_empty_.notify_one();
        return !dropped;
    }

    // ── Blocking push ─────────────────────────────────────────────────────────
    // Waits until space is available or queue is shut down.
    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] {
            return queue_.size() < max_size_ || shutdown_.load();
        });
        if (shutdown_.load()) return;
        queue_.push(std::move(item));
        lock.unlock();
        not_empty_.notify_one();
    }

    // ── Blocking pop with timeout ─────────────────────────────────────────────
    // Returns std::nullopt on timeout or shutdown.
    std::optional<T> pop(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_empty_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || shutdown_.load();
        });
        if (!ok || (queue_.empty() && shutdown_.load()))
            return std::nullopt;
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        lock.unlock();
        not_full_.notify_one();
        return item;
    }

    // ── Non-blocking pop ──────────────────────────────────────────────────────
    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        lock.unlock();
        not_full_.notify_one();
        return item;
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    // Unblocks all threads waiting on pop() or push().
    void shutdown() {
        shutdown_.store(true);
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    bool is_shutdown() const { return shutdown_.load(); }

    // ── Drain ─────────────────────────────────────────────────────────────────
    // Discard all queued items. Useful before starting a new recording.
    void drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) queue_.pop();
        not_full_.notify_all();
    }

    // ── Reset ─────────────────────────────────────────────────────────────────
    // Clear queue and re-enable after a previous shutdown.
    // Safe to call from a single controlling thread between start/stop cycles.
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) queue_.pop();
        shutdown_.store(false);
    }
    // ── Observers ─────────────────────────────────────────────────────────────
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T>           queue_;
    mutable std::mutex      mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::size_t             max_size_;
    std::atomic<bool>       shutdown_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations of item types (defined in models.hpp / events.hpp)
// ─────────────────────────────────────────────────────────────────────────────
struct VideoFrame;
struct AudioChunk;
struct PipelineEvent;
struct UploadJob;

// ─────────────────────────────────────────────────────────────────────────────
// Typed queue aliases — sizes match Python maxsize values exactly
// ─────────────────────────────────────────────────────────────────────────────
using DetectFrameQueue  = ThreadSafeQueue<VideoFrame>;   // maxsize=120
using RecordFrameQueue  = ThreadSafeQueue<VideoFrame>;   // maxsize=120
using AudioFrameQueue   = ThreadSafeQueue<AudioChunk>;   // maxsize=500
using EventQueue        = ThreadSafeQueue<PipelineEvent>;// maxsize=20
using TranscriptionQueue= ThreadSafeQueue<AudioChunk>;   // maxsize=200
using UploadQueue       = ThreadSafeQueue<UploadJob>;    // maxsize=100

} // namespace session_pipeline
