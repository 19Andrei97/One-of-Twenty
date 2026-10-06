#include <doctest/doctest.h>

#include "SharedContainer.h"

#include <memory>
#include <thread>
#include <vector>

TEST_CASE("push / pop / size / empty")
{
    SharedContainer<int> container;
    CHECK(container.empty());
    CHECK(container.size() == 0);

    container.push(1);
    container.push(2);
    CHECK(container.size() == 2);

    const auto first = container.pop();
    REQUIRE(first.has_value());
    CHECK(*first == 2); // LIFO

    const auto second = container.pop();
    REQUIRE(second.has_value());
    CHECK(*second == 1);

    CHECK(container.pop() == std::nullopt);
    CHECK(container.empty());
}

TEST_CASE("contains matches stored values")
{
    SharedContainer<int> container;
    container.push(5);
    container.push(7);

    CHECK(container.contains(5));
    CHECK(container.contains(7));
    CHECK_FALSE(container.contains(6));
}

TEST_CASE("containsIf matches on the pointee")
{
    struct Chunk { int x; };

    SharedContainer<std::shared_ptr<Chunk>> container;
    container.push(std::make_shared<Chunk>(Chunk{ 3 }));

    CHECK(container.containsIf([](const std::shared_ptr<Chunk>& c) { return c && c->x == 3; }));
    CHECK_FALSE(container.containsIf([](const std::shared_ptr<Chunk>& c) { return c && c->x == 4; }));
}

TEST_CASE("containsIf tolerates null entries")
{
    struct Chunk { int x; };

    SharedContainer<std::shared_ptr<Chunk>> container;
    container.push(nullptr);

    CHECK_FALSE(container.containsIf([](const std::shared_ptr<Chunk>& c) { return c && c->x == 1; }));
}

TEST_CASE("concurrent pushes are not lost")
{
    SharedContainer<int> container;

    constexpr int threads = 8;
    constexpr int per_thread = 2000;

    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i)
        workers.emplace_back([&container, per_thread] { for (int j = 0; j < per_thread; ++j) container.push(j); });

    for (auto& worker : workers)
        worker.join();

    CHECK(container.size() == static_cast<std::size_t>(threads * per_thread));
}
