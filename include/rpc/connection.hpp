#pragma once

#include "router.hpp"
#include <deque>
#include <mutex>

namespace mrpc {
using namespace asio::ip;
using namespace std::chrono_literals;
class router;
class server;
class client;
class connection;
struct awaitee;

constexpr uint32_t MAX_MSG_SHRINK_LEN = 1024;		// 消息缓存大于此值做收缩
constexpr uint32_t MAX_MSG_BODY_LEN = 8096*1024*10; // 最大消息长度


// 调用结果
template<typename RET>
class req_result {
  public:
    friend class connection;
    using RET_TYPE = std::conditional_t<std::is_void_v<RET>, int, RET>;
    req_result() = default;
    req_result(uint32_t err_code, const std::string& err_msg)
        : err_code_(err_code)
        , err_msg_(err_msg) {
    }

    req_result(msg_id_t id, const std::string& data) {
        auto json = router::decode(id.msg_type, data);
        if (!json.is_array() || json.size() < 2) {
            err_code_ = 408;
            err_msg_  = "response data format error.";
            return;
        }
        LOG_TRACE("request result, id: {}, content: {}", id, json.dump());
        err_code_ = json[0].get<uint32_t>();
        err_msg_ = json[1].get<std::string>();


        if constexpr (!std::is_void_v<RET>) {
            if (json.size() < 3) {
                return;
            }
            ret_ = json[2].get<RET>();
        }
    }
    // 响应码
    uint32_t error_code() {
        return err_code_;
    }
    // 错误消息
    std::string& error_msg() {
        return err_msg_;
    }
    // 返回值
    RET_TYPE value() {
        return ret_;
    }
  private:
    uint32_t err_code_ = 0;
    std::string err_msg_{ "ok" };
    RET_TYPE  ret_{};
};

struct future_msg_info {
    uint64_t msg_id;
    std::string rpc_name;
    std::promise<std::pair<msg_id_t, std::string>> promise;
};

class callback_t : asio::noncopyable, public std::enable_shared_from_this<callback_t> {
  public:
    using callback_func_type = std::function<void(uint32_t err_code, const std::string& err_msg, const nlohmann::json& ret)>;

#if (ASIO_VERSION >= 101800)
    callback_t(asio::ip::tcp::socket::executor_type& ex,
               callback_func_type cb,
               size_t timeout)
        : timer_(ex)
        , cb_(std::move(cb))
        , timeout_(timeout) {
    }
#else
    callback_t(asio::io_service& ios,
               callback_func_type cb,
               size_t timeout)
        : timer_(ios)
        , cb_(std::move(cb))
        , timeout_(timeout) {
    }
#endif

    void start_timer(const std::function<void()>& timeout_cb) {
        if (timeout_ == 0) {
            return;
        }

        timer_.expires_from_now(std::chrono::milliseconds(timeout_));
        auto self = this->shared_from_this();
        timer_.async_wait([this, self, timeout_cb](std::error_code ec) {
            if (ec) {
                return;
            }
            if (timeout_cb) timeout_cb();
            has_timeout_ = true;
        });
    }

    void callback(uint32_t err_code, const std::string& err_msg, const nlohmann::json& ret) {
        cb_(err_code, err_msg, ret);
    }

    bool has_timeout() const {
        return has_timeout_;
    }

    void cancel() {
        if (timeout_ == 0) {
            return;
        }

        std::error_code ec;
        timer_.cancel(ec);
    }

    void set_rpc_name(const std::string& rpc_name ) {
        rpc_name_ = rpc_name;
    }
    std::string& rpc_name() {
        return rpc_name_;
    }

  private:
    asio::steady_timer timer_;
    callback_func_type cb_;
    size_t timeout_ = 0;
    bool has_timeout_ = false;
    bool is_corotine_ = false;

    std::string rpc_name_;
};

// 连接对象类
class connection : public std::enable_shared_from_this<connection> {
  public:
    friend class server;
    friend class client;
    friend struct awaitee;

    static const constexpr time_t DEFAULT_TIMEOUT = 5000; //milliseconds
#ifdef _DEBUG
    static const constexpr msg_type_fmt DEFAULT_MSG_FORMAT = MSG_FMT_JSON;
#else
    static const constexpr msg_type_fmt DEFAULT_MSG_FORMAT = MSG_FMT_MSGPACK;
#endif

    using closed_callback_type = std::function<void(const std::shared_ptr<connection>&)>;
    using ptr = std::shared_ptr<connection>;
    using cptr = const std::shared_ptr<connection>&;

    connection(asio::ip::tcp::socket so, router& router)
        : socket_(std::move(so))
        , router_(router) {
    }


    router& router() {
        return router_;
    }
    uint64_t conn_id() {
        return conn_id_;
    }
    void set_conn_id(uint64_t conn_id) {
        conn_id_ = conn_id;
    }

    bool has_connected() {
        return has_connected_;
    }
    void set_connected(bool connected) {
        has_connected_ = connected;
    }

    void set_closed_callback(closed_callback_type callback) {
        closed_callback_ = callback;
    }

    void start() {
        do_read_header();
    }
    void close() {
        socket_.shutdown(asio::socket_base::shutdown_both);
    }

    bool response(msg_id_t id,
                  uint32_t err_code, const std::string& err_msg,  const nlohmann::json& ret_data) {
        nlohmann::json root{
            err_code,
            err_msg,
            ret_data,
        };
        msg_type_ &= ~(1 << MSG_IS_REQUEST);
        msg_type_ |= (1 << MSG_IS_RESPONSE);
		id.msg_type = msg_type_;
        write(id, root);
        return true;
    }

    bool response(const nlohmann::json& ret_data) {
        msg_type_ &= ~(1 << MSG_IS_REQUEST);
        msg_type_ |= (1 << MSG_IS_RESPONSE);
        return response({ msg_type_, msg_id_, req_id_ }, ok, "ok", ret_data);
    }

    bool connect(const std::string& host, uint16_t port, time_t timeout = 3) {
        async_connect(host, port);
        return wait_conn(timeout);
    }

    void async_connect(const std::string& host, uint16_t port) {
        host_ = host;
        port_ = port;
        async_connect();
    }

    void set_user_data(void* data) {
        user_data_ = data;
    }
    template<typename T>
    T* user_data() {
        return  static_cast<T*>(user_data_);
    }

    template<typename RET = void, msg_type_fmt FMT = DEFAULT_MSG_FORMAT,   typename... Args>
    auto call(const std::string& rpc_name, Args&& ... args) {
        return call<DEFAULT_TIMEOUT, RET, FMT>(rpc_name, std::forward<Args>(args)...);
    }

    template<msg_type_fmt FMT = DEFAULT_MSG_FORMAT, typename... Args>
    void notify(const std::string& rpc_name, Args&& ... args) {
        uint32_t msg_type = (1 << FMT) | (1 << MSG_IS_REQUEST) | (1 << MSG_IS_NO_RESPONSE);
        auto msg_id = router::hash(rpc_name);
        uint64_t req_id = 0;
        {
            std::unique_lock<std::mutex> lock(cb_mtx_);
            req_id = ++req_id_;
        }
		LOG_TRACE("notify, {}({})", rpc_name, msg_id);
		write({ msg_type, msg_id, req_id }, nlohmann::json::array_t{ std::forward<Args>(args)... });
    }

    template<size_t TIMEOUT, typename RET = void, msg_type_fmt FMT = DEFAULT_MSG_FORMAT, typename... Args>
    req_result<RET> call(const std::string& rpc_name, Args&&...args) {
        auto [req_id, future] = async_call(rpc_name, std::forward<Args>(args)...);
        auto status = future.wait_for(std::chrono::milliseconds(TIMEOUT));
        if (status == std::future_status::timeout || status == std::future_status::deferred) {
            {
                std::unique_lock<std::mutex> lock(cb_mtx_);
                future_map_.erase(req_id);
            }
            return req_result<RET>(408, "Request Timeout");
        }
        auto [id, msg_body] = future.get();
        return req_result<RET>(id, msg_body);
    }

    template<msg_type_fmt FMT = DEFAULT_MSG_FORMAT, typename... Args>
    auto async_call(const std::string& rpc_name, Args&&...args) {
        auto msg_id = router::hash(rpc_name);
        uint32_t msg_type = (1 << FMT) | (1 << MSG_IS_REQUEST) | (1 << MSG_IS_FUTURE);
        auto future_msg = std::make_shared<future_msg_info>();
        auto future = future_msg->promise.get_future();
        future_msg->rpc_name = rpc_name;
        future_msg->msg_id = msg_id;
        uint64_t req_id = next_req_id();
        {
            std::unique_lock<std::mutex> lock(cb_mtx_);
            future_map_.emplace(req_id, std::move(future_msg));
        }
		LOG_TRACE("async_call, {}({})", rpc_name, msg_id);
		write({ msg_type, msg_id, req_id }, nlohmann::json::array_t{ std::forward<Args>(args)... });
        return std::make_tuple(req_id, std::move(future));
    }

    template<std::size_t TIMEOUT = DEFAULT_TIMEOUT, msg_type_fmt FMT = DEFAULT_MSG_FORMAT, typename... Args>
    void async_call(callback_t::callback_func_type cb, const std::string& rpc_name, Args&&...args) {
        auto msg_id = router::hash(rpc_name);
        uint32_t msg_type = (1 << FMT) | (1 << MSG_IS_REQUEST) | (1 << MSG_IS_CALLBACK);
#if ASIO_VERSION >= 101800
        auto callback = std::make_shared<callback_t>(socket_.get_executor(), cb, TIMEOUT);
#else
        auto callback = std::make_shared<callback_t>(socket_.get_io_context(), cb, TIMEOUT);
#endif
        uint64_t req_id = next_req_id();
        {
            std::unique_lock<std::mutex> lock(this->cb_mtx_);
            callback->start_timer([this, req_id]() {
                std::unique_lock<std::mutex> lock(this->cb_mtx_);
                callback_map_.erase(req_id);
            });
            callback->set_rpc_name(rpc_name);
            callback_map_.emplace(req_id, std::move(callback));
        }
		LOG_TRACE("async_call, {}({})", rpc_name, msg_id);
		write({ msg_type, msg_id, req_id }, nlohmann::json::array_t{ std::forward<Args>(args)... });
        return;
    }

#ifdef _USE_COROUTINE
    template<typename RET = void, msg_type_fmt FMT = DEFAULT_MSG_FORMAT, typename ...Args>
    typename task_awaitable<RET> coro_call(const std::string& rpc_name, Args&&...args) {

        uint64_t req_id = next_req_id();
        auto msg_id = router::hash(rpc_name);
		uint32_t msg_type = ((1 << FMT) | (1 << MSG_IS_REQUEST) | (1 << MSG_IS_COROUTINE));
		LOG_TRACE("coro_call, {}({})", rpc_name, msg_id);
		write({ msg_type, msg_id, req_id }, nlohmann::json::array_t{ std::forward<Args>(args)... });


        auto suspend = [this, req_id](std::experimental::coroutine_handle<> h) {
            // on suspend
            std::unique_lock<std::mutex> lock(cb_mtx_);
            corotine_map_[req_id] = h;
        };
        auto resume = [this, req_id]() -> req_result<RET> {
            // on resume
            return req_result<RET>({msg_type_, msg_id_, req_id_}, msg_body_);
        };
        return task_awaitable<RET>(suspend, resume);
    }
#endif

    uint16_t msg_type() {
        return msg_type_;
    }
    uint64_t msg_id() {
        return msg_id_;
    }
    uint64_t req_id() {
        return req_id_;
    }
    uint64_t next_req_id() {
        return ++write_req_id_;
    }
    msg_id_t id() {
        return { msg_type_, msg_id_, req_id_};
    }

  private:
    // connect
    void async_connect() {
        auto addr = asio::ip::address::from_string(host_);
        socket_.async_connect(tcp::endpoint(addr, port_), [this](std::error_code ec) {
            if (has_connected_ == true) {
                return;
            }
            if (ec) {
                if (reconnect_cnt_ <= 0) {
                    conn_cond_.notify_one();
                    return;
                } else {
                    reconnect_cnt_--;
                }
                async_reconnect();
            } else {
                has_connected_ = true;
                conn_cond_.notify_one();
                start();
            }
        });
    }

    void async_reconnect() {
        reset_socket();
        async_connect();
        std::this_thread::sleep_for(std::chrono::milliseconds(1s));
    }

    void reset_socket() {
        std::error_code igored_ec;
        socket_.close(igored_ec);
#if ASIO_VERSION >= 101800
        socket_ = decltype(socket_)(socket_.get_executor());
#else
        socket_ = decltype(socket_)(socket_.get_io_service());
#endif
        if (!socket_.is_open()) {
            socket_.open(asio::ip::tcp::v4());
        }
    }

    void on_closed() {
        if (socket_.is_open()) {
            std::error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_both, ec);
            socket_.close(ec);
        }
        if (closed_callback_ && conn_id_ > 0) closed_callback_(shared_from_this());
    }

    bool wait_conn(time_t timeout) {
        if (has_connected_) {
            return true;
        }

        has_wait_ = true;
        std::unique_lock<std::mutex> lock(conn_mtx_);
        bool result = conn_cond_.wait_for(lock, std::chrono::seconds(timeout),
                                          [this] {return has_connected_.load(); });
        has_wait_ = false;
        return has_connected_;
    }

    // do read
    void do_read_header() {
        auto self(shared_from_this());
        std::array<asio::mutable_buffer, 4> buffers;
        buffers[0] = asio::buffer(&msg_type_, sizeof(msg_type_));
        buffers[1] = asio::buffer(&msg_id_, sizeof(msg_id_));
        buffers[2] = asio::buffer(&req_id_, sizeof(req_id_));
        buffers[3] = asio::buffer(&msg_len_, sizeof(msg_len_));
        asio::async_read(socket_, buffers, [this, self](std::error_code ec, std::size_t /*length*/) {
            if (ec) {
                if (ec != asio::error::eof && ec != asio::error::connection_reset) {
                    LOG_ERROR("read header error: {} code：{} ", ec.message(), ec.value());
                }
                on_closed();
                return;
            }
            if (msg_len_ > MAX_MSG_BODY_LEN) {
                on_closed();
                LOG_ERROR("msg length over max: {}", msg_len_);
                return;
            }
            if (msg_id_ == 0) {
                // 心跳包
            }
            do_read_body();
        });
    }
    void do_read_body() {
        auto self(shared_from_this());

        msg_body_.resize(msg_len_);
        asio::async_read(socket_, asio::buffer(const_cast<char*>(msg_body_.data()), msg_body_.length()),
        [this, self](std::error_code ec, std::size_t /*length*/) {
            if (ec) {
                if (ec == asio::error::eof) {
                    on_closed();
                    return;
                }
                LOG_ERROR("read header error: {} code: {}", ec.message(), ec.value());
                return;
            }
            msg_id_t id{ msg_type_, msg_id_, req_id_ };
            if (msg_type_ & (1 << MSG_IS_REQUEST)) {
                router_.route_request<connection>(self, id, msg_body_);
            } else 	if (msg_type_ & (1 << MSG_IS_RESPONSE)) {
                if (msg_type_ & (1 << MSG_IS_FUTURE)) {
                    on_future_response(id);
                } else if (msg_type_ & (1 << MSG_IS_CALLBACK)) {
                    on_callback_response(id);
                } else if (msg_type_ & (1 << MSG_IS_COROUTINE)) {
                    on_coroutine_response(id);
                }
            }

            if (msg_body_.length() > MAX_MSG_SHRINK_LEN) {
                // 正常消息不超过8k
                msg_body_.clear();
                msg_body_.shrink_to_fit();
            }
            do_read_header();
        });
    }



    // do write
    void write(msg_id_t id, const nlohmann::json &json) {
        std::string data;
        if (id.msg_type & (1 << MSG_FMT_JSON)) {
            data = json.dump();
        } else if (id.msg_type & (1 << MSG_FMT_BJSON)) {
            nlohmann::json::to_bson(json, data);
        } else if (id.msg_type & (1 << MSG_FMT_UBJSON)) {
            nlohmann::json::to_ubjson(json, data);
        } else if (id.msg_type & (1 << MSG_FMT_MSGPACK)) {
            nlohmann::json::to_msgpack(json, data);
        } else if (id.msg_type & (1 << MSG_FMT_CBOR)) {
            nlohmann::json::to_cbor(json, data);
        }
		{
			std::lock_guard<std::mutex> locker(write_mtx_);
			write_queue_.emplace_back(id, std::move(data));
			if (write_queue_.size() > 1) {
				// 等待write()回调处理
				return;
			}
		}
		write();
    }

  private:
    void on_future_response(msg_id_t id) {
        std::lock_guard<std::mutex> lock(cb_mtx_);
        auto iter = future_map_.find(id.req_id);
        if (iter != future_map_.end()) {
            iter->second->promise.set_value({id, msg_body_});
            future_map_.erase(iter);
        } else {
			LOG_ERROR("recv unknow rpc response, msg id: {}", id);
        }
    }

    void on_callback_response(msg_id_t id) {
        decltype(callback_map_)::mapped_type pcallback;
        {
            std::lock_guard<std::mutex> lock(cb_mtx_);
            auto iter = callback_map_.find(req_id_);
            if (iter != callback_map_.end()) {
                LOG_TRACE("rpc response, msg id: {}({})",
                          iter->second->rpc_name(), id);
                pcallback = iter->second;
                callback_map_.erase(iter);
            }
        }
        if (pcallback == nullptr) {
            LOG_ERROR("recv unknow rpc response, msg id: {} ", id);
            return;
        }
        uint32_t err_code = 0;
        std::string err_msg{ "ok" };
        nlohmann::json* pret = nullptr;

        auto json = router::decode(id.msg_type, msg_body_);
        LOG_TRACE("rpc response, msg id: {}({}), content: {}",
                          pcallback->rpc_name(), id, json.dump());
        if (!json.is_array() || json.size() < 2) {
            err_code = 408;
            err_msg = "response data format error.";
            pcallback->callback(err_code, err_msg, nullptr);
            return;
        }
        err_code = json[0].get<uint32_t>();
        err_msg = json[1].get<std::string>();
        if (json.size() > 2) {
            pret = &json[2];
        }
        pcallback->callback(err_code, err_msg, (pret == nullptr ? nlohmann::json() : *pret));
    }

    void on_coroutine_response(msg_id_t id) {
        std::experimental::coroutine_handle<> h;
        {
            std::lock_guard<std::mutex> lock(cb_mtx_);
            auto iter = corotine_map_.find(id.req_id);
            if (iter != corotine_map_.end()) {
                h = iter->second;
                corotine_map_.erase(iter);
            } else {
                LOG_WARN("corotine not found, req id: {}, msg_id: {}", id.req_id, id.msg_id);
            }
        }
        if (h) h.resume();
    }

	void write() {
		if (!socket_.is_open()) {
			return;
		}
		std::array<asio::const_buffer, 5> buffers;
		{
			std::lock_guard<std::mutex> locker(write_mtx_);
			if (write_queue_.empty()) return;

			auto& pair = write_queue_.front();
			auto& id = pair.first;
			auto& data = pair.second;

			uint32_t msg_len = static_cast<uint32_t>(data.length());
			buffers[0] = asio::buffer(&id.msg_type, sizeof(id.msg_type));
			buffers[1] = asio::buffer(&id.msg_id, sizeof(id.msg_id));
			buffers[2] = asio::buffer(&id.req_id, sizeof(id.req_id));
			buffers[3] = asio::buffer(&msg_len, sizeof(msg_len));
			buffers[4] = asio::buffer(data.data(), msg_len);
		}
		auto self(shared_from_this());
		asio::async_write(socket_, buffers, [this, self](const std::error_code& ec, const size_t length) {
			if (ec) {
				on_closed();
				return;
			}
			std::unique_lock<std::mutex> lock(write_mtx_);
			write_queue_.pop_front();

			if (!write_queue_.empty()) {
				lock.unlock();

				write();
			}
		});
	}


  private:
    mrpc::router& router_;
    asio::ip::tcp::socket socket_;
    uint64_t conn_id_ = 0;
    // client infomation
    std::string host_;
    uint16_t port_ = 0;
    std::atomic_bool has_connected_ = false;
    int32_t reconnect_cnt_ = -1;
    bool has_wait_ = false;
    std::mutex conn_mtx_;
    std::condition_variable conn_cond_;

    std::atomic_uint64_t write_req_id_ = 0;
    // msg header
    uint32_t msg_type_ = 0;
    uint64_t msg_id_ = 0;
    uint64_t req_id_ = 0;
    uint32_t msg_len_ = 0;
    // msg body
    std::string msg_body_;

    closed_callback_type closed_callback_;

    std::mutex cb_mtx_;
    std::unordered_map<uint64_t, std::shared_ptr<future_msg_info>> future_map_;
    std::unordered_map<uint64_t, std::shared_ptr<callback_t>> callback_map_;
    std::unordered_map<uint64_t, std::experimental::coroutine_handle<>> corotine_map_;

	std::mutex write_mtx_;
	std::deque<std::pair<msg_id_t, std::string>> write_queue_;

    void* user_data_ = nullptr;
};

} // namespace mrpc
