#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <algorithm>
#include <optional>
#include <utility>

// THREAD SAFE CONTAINER
template<typename T>
class SharedContainer 
{
    std::vector<T> container;
    mutable std::mutex mtx;

public:
    void push(T data) 
    {
        std::lock_guard<std::mutex> lock(mtx);

        container.push_back(std::move(data));
    }

    bool contains(const T& data)
    {
        std::lock_guard<std::mutex> lock(mtx);

        return std::find(container.begin(), container.end(), data) != container.end();
    }

    // Generic membership test; the predicate receives a const T&.
    // Callers that store pointers can still match on the pointee, e.g.
    //   containsIf([&](const auto& chunk) { return chunk->position == pos; })
    template<typename Predicate>
    bool containsIf(Predicate pred)
    {
        std::lock_guard<std::mutex> lock(mtx);

        return std::find_if(container.begin(), container.end(), pred) != container.end();
    }

    std::optional<T> pop() 
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (container.empty()) return std::nullopt;

        T value = std::move(container.back());
        container.pop_back();
        return value;
    }

    bool empty() const 
    {
        std::lock_guard<std::mutex> lock(mtx);

        return container.empty();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mtx);

        return container.size();
    }
};

