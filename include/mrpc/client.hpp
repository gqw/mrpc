#ifndef MRPC_CLIENT_HPP
#define MRPC_CLIENT_HPP

#pragma once

#include <asio.hpp>
#include "router.hpp"
#include "connection.hpp"

namespace mrpc {
using namespace asio::ip;
class connection;

/**
 *  client for global
 */
class client final : private asio::noncopyable {
  public:
    /**
     * singleton for client
     */
    static client& get() {
        static client obj;
        return obj;
    }

    /**
     * export router object
     */
    mrpc::router& router() {
        return router_;
    }

    std::shared_ptr<connection> connect(const std::string& host, uint16_t port,
                                        std::time_t timeout = connection::DEFAULT_TIMEOUT) {
        auto conn = std::make_shared<connection>(asio::ip::tcp::socket(get_iocontext()), router_);
        if (conn == nullptr) return nullptr;
        return conn->connect(host, port, timeout) ? conn : nullptr;
    }

    std::shared_ptr<connection> async_connect(const std::string& host, uint16_t port) {
        auto conn = std::make_shared<connection>(asio::ip::tcp::socket(get_iocontext()), router_);
        if (conn == nullptr) return nullptr;
        conn->async_connect(host, port);
        return conn;
    }

    /**
     *  use one thread per iocontex, and auto runing
     *
     * @param io_count io_context pool size, default is double cpu count
     * @param thread_per_io thread count per io_context
     */
    void run(std::size_t io_count = 0,
             std::size_t thread_per_io = 1) {
        if (is_running_) return; // prevent call repeated.
        if (io_count < 1) {
            io_count = std::thread::hardware_concurrency() * 2;
        }
		iocs_.clear();
		for (std::size_t i = 0; i < io_count; ++i) {
			auto ioc = std::make_shared<asio::io_context>();
			iocs_.push_back(ioc);
            // assign a work, or io will stop
            workds_.emplace_back(std::make_shared<asio::io_context::work>(*ioc));
            for (std::size_t i = 0; i < thread_per_io; ++i) {
                thread_pool_.emplace_back([ioc]() {
                    ioc->run();
                });
            }
        }
        is_running_ = true;
        LOG_INFO("client runing ...");
    }

    /**
     *  shutdown all services and threads
     */
    void shutdown() {
        for (auto& ioc : iocs_) {
            ioc->stop();
        }
    }

    /**
     * wait all server and thread stoped
     */
    void wait_shutdown() {
        for (auto& thread : thread_pool_) {
            thread.join();
        }
    }

  private:
    client() {}
    ~client() {
        shutdown();
    }

    asio::io_context& get_iocontext() {
        // round-robin
        if (iocs_.size() < 2) {
            return *(iocs_.at(0));
        }
        ++next_ioc_index_;
        if (next_ioc_index_ >= iocs_.size()) {
            next_ioc_index_ = 1; // the first io_context only for accept
        }
        auto& ioc = iocs_[next_ioc_index_];
        return *ioc;
    }

  private:
	mrpc::router router_;

    std::atomic_bool is_running_ = false;                   // check is running, prevent multiple call run functions
    std::atomic_uint64_t next_ioc_index_ = 0;               // use atomic ensure thread safe
    std::vector<std::shared_ptr<asio::io_context>> iocs_;   // io pool
    std::vector<std::thread> thread_pool_;                  // thread pool
    std::vector<std::shared_ptr<asio::io_context::work>> workds_;
};
} // namespace mrpc

#endif // MRPC_CLIENT_HPP

