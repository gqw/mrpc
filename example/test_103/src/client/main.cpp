#include <logger.hpp>
#include <rpc/client.hpp>
#include <rpc/coroutine.hpp>

#include <iostream>
#include <thread>
#include <chrono>
using namespace std::chrono_literals;
using namespace mrpc;

task<void> test_coro2(connection::cptr conn, int thread_index, int s) {
    for (size_t i = 0; i < 5; i++)
    {
        auto ret = co_await conn->coro_call<int>("test", i);

        if (ret.error_code() != ok || ret.value() != i + 1) {
            LOG_WARN("{}-{} ret failed: {}-{} ({}<->{})", thread_index, s, ret.error_code(), ret.error_msg(), i, ret.value());
        } else {
            LOG_DEBUG("{}-{} ret: {}-{} ({}<->{})", thread_index, s, ret.error_code(), ret.error_msg(), i, ret.value());
        }
    }
    co_return;
}

task<uint32_t> test_coro(connection::cptr conn) {
    auto ret = co_await conn->coro_call<int>("test_add", 1, 1);  // 1+1
    ret = co_await conn->coro_call<int>("test_add", ret.value(), ret.value()); // 2+2
    ret = co_await conn->coro_call<int>("test_add", ret.value(), ret.value()); // 4+4
    ret = co_await conn->coro_call<int>("test_add", ret.value(), ret.value()); // 8+8
    LOG_DEBUG("coroutine final return: {}", ret.value());
    co_return ret.value(); // 16
}

int main() {
    wlog::logger::get().init("logs/" PROJECT_NAME ".log");
    LOG_TRACE("main thread: {}", std::this_thread::get_id());

    std::vector<std::thread> ts;
    for (size_t i = 0; i < 1; i++)
    {
        ts.push_back(std::move(std::thread([](){
            LOG_TRACE("work thread: {}", std::this_thread::get_id());

            client::get().run();
        })));
    }

    auto conn = client::get().connect("127.0.0.1", 3333);
    if (conn == nullptr) return 1;

    test_coro(conn);

    for (auto &&t : ts)
    {
        t.join();
    }

    client::get().shutdown();
	wlog::logger::get().shutdown();
    return 0;
}