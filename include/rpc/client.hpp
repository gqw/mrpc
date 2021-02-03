#pragma once

#include <asio.hpp>
#include "router.hpp"
#include "connection.hpp"

namespace rpc
{
	using namespace asio::ip;

	class connection;
	class client final : private asio::noncopyable
	{
		friend class rpc::connection;
	public:
		static client& get() {
			static client obj;
			return obj;
		}

		asio::io_context& io_context() { return ioc_; }
		rpc::router& router() { return router_; }

		std::shared_ptr<connection> connect(const std::string& host, uint16_t port, std::time_t timeout = 3) {
			auto conn = std::make_shared<connection>(asio::ip::tcp::socket(ioc_), router_);
			return conn->connect(host, port, timeout) ? conn : nullptr;
		}

		std::shared_ptr<connection> async_connect(const std::string& host, uint16_t port) {
			auto conn = std::make_shared<connection>(asio::ip::tcp::socket(ioc_), router_);
			conn->async_connect(host, port);
			return conn;
		}

		void run() {
			if (ioc_.stopped()) {
				ioc_.restart();
			}
			else {
				ioc_.run();
			}
		}

		void shutdown() {
			for (auto& conn : conns_)
			{
				auto c = conn.lock();
				if (c) {
					c->close();
				}
			}
			conns_.clear();
			if (ioc_.stopped() == false) ioc_.stop();
			if (work_thread_.joinable()) {
				work_thread_.join();
			}
		}

	private:
		client() : work_(ioc_) {}
		~client() { shutdown(); }

	private:
		asio::io_context ioc_;
		rpc::router router_;
		asio::io_context::work work_;

		std::thread work_thread_;

		std::list<std::weak_ptr<rpc::connection>> conns_;
	};
}
