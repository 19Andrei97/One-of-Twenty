#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <optional>

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

    bool contains(T& data)
    {
        std::lock_guard<std::mutex> lock(mtx);

        return std::find(container.begin(), container.end(), data) != container.end();
    }

    bool containsPosition(sf::Vector2i& pos)
    {
        std::lock_guard<std::mutex> lock(mtx);

        for (auto& chunk : container)
        {
            if (chunk->position == pos)
                return true;
        }
        
        return false;
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

