#ifndef MRPC_SERVER_HPP
#define MRPC_SERVER_HPP
#pragma once

#include "connection.hpp"

namespace mrpc {
using namespace asio::ip;
class connection;

/**
 *  Server for global
 */
class server final : private asio::noncopyable {
  public:
    /**
     * singleton for server
     */
    static server& get() {
        static server obj;
        return obj;
    }

    /**
     * create all acceptor with main iocontext. and do accept
     *
     * @param host listening host, normally it is "0.0.0.0"
     * @param port listening socket port, if pass 0 then system will random a port
     *
     * @return if no exception return true
     */
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

    /**
     *  assign io context and threads
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
        LOG_INFO("server runing ...");
    }

    /**
     *  run once and call by user
     */
    void run_once() {
        for (auto& ioc : iocs_) {
            ioc->poll_one();
        }
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

    /**
     * got main io object
     */
    asio::io_context& main_iocontext() {
        if (iocs_.empty())
            throw std::logic_error("server not running!!!");
        return *(iocs_.at(0)); // the first io_context only for accept
    }

    /**
     * export router object
     */
    mrpc::router& router() {
        return router_;
    }

    /**
     *  sometime we need random local listening port, so need tell user
     *  the real port we are listening
     */
    uint16_t port() {
        if (acceptor_ == nullptr) return 0;
        if (acceptor_->is_open() == false) return 0;
        return acceptor_->local_endpoint().port();
    }

  private:
    server() {
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
    std::atomic_bool is_running_ = false;                   // check is running, prevent multiple call run functions
    std::atomic_size_t next_ioc_index_ = 0;               // use atomic ensure thread safe
    std::vector<std::shared_ptr<asio::io_context>> iocs_;   // io pool
    std::vector<std::thread> thread_pool_;                  // thread pool
    std::vector<std::shared_ptr<asio::io_context::work>> workds_;
    std::shared_ptr<tcp::acceptor> acceptor_;

    mrpc::router router_;
};
} // namespace mrpc

#endif // MRPC_SERVER_HPP