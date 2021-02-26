#pragma once

#include <asio.hpp>
#include "router.hpp"
#include "connection.hpp"

namespace mrpc {
using namespace asio::ip;
class connection;

class client final : private asio::noncopyable {
    friend class connection;
  public:
    static client& get() {
        static client obj;
        return obj;
    }

    void run();
    void shutdown();

    asio::io_context& io_context() {  return ioc_; }
    router& router() { return router_; }

    connection::ptr connect(const std::string& host, uint16_t port,
                            std::time_t timeout = 3);

    connection::ptr async_connect(const std::string& host, uint16_t port);

  private:
    client() : work_(ioc_) {}
    ~client() { shutdown(); }

  private:
    asio::io_context ioc_;
	mrpc::router router_;
    asio::io_context::work work_;

    std::thread work_thread_;
};

connection::ptr client::connect(const std::string& host, uint16_t port,
                                std::time_t timeout/*= 3*/) {
    auto conn = std::make_shared<connection>(asio::ip::tcp::socket(ioc_), router_);
    return conn->connect(host, port, timeout) ? conn : nullptr;
}

connection::ptr client::async_connect(const std::string& host, uint16_t port) {
    auto conn = std::make_shared<connection>(asio::ip::tcp::socket(ioc_), router_);
    conn->async_connect(host, port);
    return conn;
}

void client::run() {
    if (ioc_.stopped()) {
        ioc_.restart();
    } else {
        ioc_.run();
    }
}

void client::shutdown() {
    if (ioc_.stopped() == false) ioc_.stop();
    if (work_thread_.joinable()) {
        work_thread_.join();
    }
}
} // namespace mrpc
