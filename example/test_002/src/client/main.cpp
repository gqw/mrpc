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

    conn->async_call([](uint32_t err_code, const std::string& err_msg, const nlohmann::json& ret){
        if (err_code == mrpc::ok) {
            std::cout << "return: " << ret.get<uint32_t>() << std::endl;
        } else {
            std::cout << "return error: " << err_msg << std::endl;
        }
    }, "test_add", 11, 12);

    t.join();
    wlog::logger::get().shutdown();
    return 0;
}