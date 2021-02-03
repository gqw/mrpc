#pragma once

#include <asio.hpp>
// #include <asio/ssl.hpp>

#include "router.hpp"
#include "connection.hpp"
namespace rpc
{
	using namespace asio::ip;

	class connection;
	class server : private asio::noncopyable
	{
		friend class rpc::connection;
	public:
		static server& get() {
			static server obj;
			return obj;
		}

		bool init(std::size_t thread_count) {
			for (std::size_t i = 0; i < thread_count; i++)
			{
				iocs_.emplace_back(std::make_shared<asio::io_context>());
			}
			return true;
		}

		rpc::router& router() { return router_; }
		uint16_t port() {
			if (acceptor_ == nullptr) return 0;
			if (acceptor_->is_open() == false) return 0;
			return acceptor_->local_endpoint().port();
		}

		bool do_accept(const std::string& host, uint16_t port) {
			try {
				tcp::endpoint endpoint(tcp::v4(), port);
				endpoint.address(asio::ip::address_v4::from_string(host));
				acceptor_ = std::make_shared<asio::ip::tcp::acceptor>(main_iocontext(), endpoint);

				LOG_INFO("server listening on: {}", endpoint);
			}
			catch (asio::system_error e) {
				LOG_ERROR("accept error: {} code: {}", e.what(), e.code());
				return false;
			}

			do_accept();
			return true;
		}

		void run() {
			for (std::size_t i = 1; i < iocs_.size(); ++i)
			{
				auto& ioc = iocs_[i];
				workds_.emplace_back(std::make_shared<asio::io_context::work>(*ioc));
				thread_pool_.emplace_back([this, &ioc]() {
					ioc->run();
				});
			}
			LOG_INFO("server runing ...");
			auto& main_ioc = iocs_[0];
			workds_.emplace_back(std::make_shared<asio::io_context::work>(*main_ioc));
			main_ioc->run();
		}

		void run_once() {
			for (auto& ioc : iocs_)
			{
				ioc->poll_one();
			}
		}

		void stop() {
			for (auto& ioc : iocs_)
			{
				ioc->stop();
			}
			for (auto& thread : thread_pool_)
			{
				thread.join();
			}
		}

		asio::io_context& main_iocontext() {
			return *(iocs_.at(0)); // the first io_context only for accept
		}

	private:
		server() {
			iocs_.emplace_back(std::make_shared<asio::io_context>());
		}

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
					LOG_ERROR("accept error: {} code: {}", ec.message() , ec.value());
					return;
				}
				auto conn = std::make_shared<connection>(std::move(socket), router_);
				// conn->has_connected_ = true;
				conn->set_conn_id(++conn_id_);
				conn->set_connected(true);
				conn->start();
				conn->set_closed_callback([this](connection::cptr conn) {
					std::lock_guard<std::mutex> lock(mutex_);
					connections_.erase(conn->conn_id());
				});
				{
					std::lock_guard<std::mutex> lock(mutex_);
					connections_.emplace(conn->conn_id(), conn);
				}
				do_accept();
			});
		}

	private:
		std::size_t next_ioc_index = 0;
		std::vector<std::shared_ptr<asio::io_context>> iocs_;
		std::vector<std::thread> thread_pool_;
		std::vector<std::shared_ptr<asio::io_context::work>> workds_;
		std::shared_ptr<tcp::acceptor> acceptor_;

		rpc::router router_;

		std::mutex mutex_;
		std::atomic_uint64_t conn_id_ = 0;
		std::unordered_map<uint64_t, std::shared_ptr<connection>> connections_;
	};
}
