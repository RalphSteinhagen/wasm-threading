#ifndef LOCKFREEQUEUE_HPP
#define LOCKFREEQUEUE_HPP

#include <atomic>
#include <array>
#include <optional>

/**
 * @brief Lock-free single-producer single-consumer (SPSC) queue with deque-style interface.
 *
 * ## Example Usage:
 * @code
 * LockFreeQueue<int, 8> queue;
 * queue.push_back(42);
 * queue.push_front(1);
 *
 * if (auto val = queue.pop_front()) {
 *     std::cout << *val << std::endl;
 * }
 * @endcode
 */
template<typename T, std::size_t Capacity>
struct LockFreeQueue {
    std::array<T, Capacity>  _buffer;
    std::atomic<std::size_t> _head{0}; // points to front element
    std::atomic<std::size_t> _tail{0}; // points to one past the last element

    constexpr std::size_t next(std::size_t i) const noexcept { return (i + 1) % Capacity; }
    constexpr std::size_t prev(std::size_t i) const noexcept { return (i + Capacity - 1) % Capacity; }
    bool                  empty() const noexcept { return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire); }
    bool                  full() const noexcept { return next(_tail.load(std::memory_order_acquire)) == _head.load(std::memory_order_acquire); }

    bool push_back(const T& item) {
        auto tail      = _tail.load(std::memory_order_relaxed);
        auto next_tail = next(tail);
        if (next_tail == _head.load(std::memory_order_acquire))
            return false; // full
        _buffer[tail] = item;
        _tail.store(next_tail, std::memory_order_release);
        return true;
    }

    bool push_front(const T& item) {
        auto head     = _head.load(std::memory_order_relaxed);
        auto new_head = prev(head);
        if (_tail.load(std::memory_order_acquire) == new_head) {
            return false; // full
        }
        _buffer[new_head] = item;
        _head.store(new_head, std::memory_order_release);
        return true;
    }

    std::optional<T> pop_front() {
        auto head = _head.load(std::memory_order_relaxed);
        if (head == _tail.load(std::memory_order_acquire)) {
            return std::nullopt; // empty
        }
        T item = std::move(_buffer[head]);
        _head.store(next(head), std::memory_order_release);
        return item;
    }

    std::optional<T> pop_back() {
        auto tail = _tail.load(std::memory_order_relaxed);
        if (_head.load(std::memory_order_acquire) == tail) {
            return std::nullopt; // empty
        }
        auto new_tail = prev(tail);
        T    item     = std::move(_buffer[new_tail]);
        _tail.store(new_tail, std::memory_order_release);
        return item;
    }

    std::optional<T> front() const {
        auto head = _head.load(std::memory_order_acquire);
        if (head == _tail.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        return _buffer[head];
    }

    std::optional<T> back() const {
        auto tail = _tail.load(std::memory_order_acquire);
        if (_head.load(std::memory_order_acquire) == tail) {
            return std::nullopt;
        }
        return _buffer[prev(tail)];
    }
};

#endif //LOCKFREEQUEUE_HPP