#include <logger.hpp>
#include <rpc/client.hpp>

#include <iostream>
#include <thread>
#include <chrono>
using namespace std::chrono_literals;
using namespace mrpc;

int main() {
    wlog::logger::get().init("logs/" PROJECT_NAME ".log");

    std::thread t([](){
        client::get().run();
    });
    auto conn = client::get().connect("127.0.0.1", 3333);
    if (conn == nullptr) return 1;

    auto ret = conn->call<uint32_t>("test_add", 11, 12);
    if (ret.error_code() == mrpc::ok) {
        std::cout << "return: " << ret.value() << std::endl;
    } else {
        std::cout << "return error: " << ret.error_msg() << std::endl;
    }

    t.join();
    std::this_thread::sleep_for(5s);

    wlog::logger::get().shutdown();
    return 0;
}