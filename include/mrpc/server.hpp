#ifndef MRPC_SERVER_HPP
#define MRPC_SERVER_HPP
#pragma once

#include "connection.hpp"

namespace mrpc {
using namespace asio::ip;
class connection;

/// Server for global
class server final : private asio::noncopyable {
    friend class connection;
  public:
    // singleton
    static server& get() {
        static server obj;
        return obj;
    }

    // initialize server's io_context pool.
    bool init(std::size_t thread_count) {
        for (std::size_t i = 0; i < thread_count; i++) {
            iocs_.emplace_back(std::make_shared<asio::io_context>());
        }
        return true;
    }

    // create all acceptor with main iocontext. and do accept
    bool do_accept(const std::string& host, uint16_t port) {
        try {
            tcp::endpoint endpoint(tcp::v4(), port);
            endpoint.address(asio::ip::address_v4::from_string(host));
            acceptor_ = std::make_shared<asio::ip::tcp::acceptor>(main_iocontext(), endpoint);

            LOG_INFO("server listening on: {}", endpoint);
        } catch (asio::system_error& e) {
            LOG_ERROR("accept error: {} code: {}", e.what(), e.code());
            return false;
        }

        do_accept();
        return true;
    }

    // use one thread per iocontex, and auto runing
    void run() {
        for (std::size_t i = 0; i < iocs_.size(); ++i) {
            auto& ioc = iocs_[i];
            // assign a work, or io will stop
            workds_.emplace_back(std::make_shared<asio::io_context::work>(*ioc));
            thread_pool_.emplace_back([&ioc]() {
                ioc->run();
            });
        }
        LOG_INFO("server runing ...");
    }

    // run once and call by user
    void run_once() {
        for (auto& ioc : iocs_) {
            ioc->poll_one();
        }
    }

    // show down all services and threads
    void shutdown() {
        for (auto& ioc : iocs_) {
            ioc->stop();
        }
        for (auto& thread : thread_pool_) {
            thread.join();
        }
    }

    asio::io_context& main_iocontext() {
        return *(iocs_.at(0)); // the first io_context only for accept
    }

    // export router object
    mrpc::router& router() {
        return router_;
    }

    // sometime we need random local listening port, so need tell user
    // the real port we are listening
    uint16_t port() {
        if (acceptor_ == nullptr) return 0;
        if (acceptor_->is_open() == false) return 0;
        return acceptor_->local_endpoint().port();
    }

  private:
    server() {
        // ensure we have one io_context
        iocs_.emplace_back(std::make_shared<asio::io_context>());
    }

    // round-robin
    asio::io_context& get_iocontext() {
        if (iocs_.size() < 2) {
            return *(iocs_.at(0));
        }
        ++next_ioc_index;
        if (next_ioc_index >= iocs_.size()) {
            next_ioc_index = 1; // the first io_context only for accept
        }
        auto& ioc = iocs_[next_ioc_index];
        return *ioc;
    }

    void do_accept() {
        auto& ioc = get_iocontext();
        acceptor_->async_accept(ioc, [this](std::error_code ec, tcp::socket socket) {
            if (ec) {
                LOG_ERROR("accept error: {} code: {}", ec.message(), ec.value());
                return;
            }
            // create new connection
            auto conn = std::make_shared<connection>(std::move(socket), router_);
            conn->set_connected(true);
            conn->start(); // start to wait read data from network
            do_accept();
        });
    }

  private:
    std::size_t next_ioc_index = 0;
    std::vector<std::shared_ptr<asio::io_context>> iocs_;   // io pool
    std::vector<std::thread> thread_pool_;                  // thread pool
    std::vector<std::shared_ptr<asio::io_context::work>> workds_;
    std::shared_ptr<tcp::acceptor> acceptor_;

    mrpc::router router_;
};
} // namespace mrpc

#include "router.ipp"

#endif // MRPC_SERVER_HPP