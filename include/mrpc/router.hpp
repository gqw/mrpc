#ifndef MRPC_ROUTER_HPP
#define MRPC_ROUTER_HPP

#pragma once

#include <chrono>
#include <functional>
#include <limits>

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "coroutine.hpp"

#if !defined (RPC_USE_LOG)
#	define LOG_TRACE(fmt, ...)
#	define LOG_DEBUG(fmt, ...)
#	define LOG_INFO(fmt, ...)
#	define LOG_WARN(fmt, ...)
#	define LOG_ERROR(fmt, ...)
#endif // RPC_USE_LOG

namespace mrpc {
struct awaitee;
enum msg_type_bits {
    MSG_IS_REQUEST,
    MSG_IS_RESPONSE,
    MSG_IS_NO_RESPONSE,
    MSG_IS_FUTURE,
    MSG_IS_CALLBACK,
    MSG_IS_COROUTINE,
    MSG_IS_MULTIPLE,
    MSG_IS_BROADCAST,
};

enum msg_type_fmt {
    MSG_FMT_RAW = 16,
    MSG_FMT_JSON,
    MSG_FMT_BJSON,
    MSG_FMT_UBJSON,
    MSG_FMT_MSGPACK,
    MSG_FMT_CBOR,
};

enum status {
    init,
    switching_protocols = 101,
    ok = 200,
    created = 201,
    accepted = 202,
    no_content = 204,
    partial_content = 206,
    multiple_choices = 300,
    moved_permanently = 301,
    moved_temporarily = 302,
    not_modified = 304,
    temporary_redirect = 307,
    bad_request = 400,
    unauthorized = 401,
    forbidden = 403,
    not_found = 404,
    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503
};

struct msg_id_t {
    // message_type msg_type = message_type::MSG_NONE;
    uint32_t msg_type = 0;
    uint64_t msg_id = 0;
    uint64_t req_id = 0;
};

inline std::ostream& operator<<(std::ostream& os, const msg_id_t& id) {
    os << "M:" << id.msg_id << " R:" << id.req_id;
    return os;
}

template<typename T>
using remove_const_reference_t = std::remove_const_t<std::remove_reference_t<T>>;

template<typename>
struct function_traits;

template<typename Function>
struct function_traits : public function_traits<decltype(&Function::operator())> {
};

template<typename ReturnType, typename Arg, typename... Args>
struct function_traits<ReturnType(*)(Arg, Args...)> {
    enum {
        argc = sizeof...(Args),
        arity,
    };
    typedef ReturnType function_type(Arg, Args...);
    using stl_function_type = std::function<function_type>;
    typedef ReturnType(*pointer)(Args...);
    using return_type = ReturnType;
    using args_tuple = std::tuple<remove_const_reference_t<Args>...>;
};

template <typename ReturnType, typename... Args>
struct function_traits<std::function<ReturnType(Args...)>> : function_traits<ReturnType(*)(Args...)> {};

template <typename ReturnType, typename ClassType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...)> : function_traits<ReturnType(*)(Args...)> {};

template <typename ReturnType, typename ClassType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...) const> : function_traits<ReturnType(*)(Args...)> {};


class connection;
class router {
  public:
    template<typename Function, typename SelfClass>
    static bool invoke_callback(Function f, SelfClass* self,
                                const std::shared_ptr<connection>& conn,
								const std::string& func_name,
                                msg_id_t id,
                                const std::string& buffer);
    static nlohmann::json decode(uint32_t msg_type, const std::string& buffer) {
        nlohmann::json json;
        if (msg_type & (1 << MSG_FMT_JSON)) {
            json = nlohmann::json::parse(buffer);
        } else if (msg_type & (1 << MSG_FMT_BJSON)) {
            json = nlohmann::json::from_bson(buffer);
        } else if (msg_type & (1 << MSG_FMT_UBJSON)) {
            json = nlohmann::json::from_ubjson(buffer);
        } else if (msg_type & (1 << MSG_FMT_MSGPACK)) {
            json = nlohmann::json::from_msgpack(buffer);
        } else if (msg_type & (1 << MSG_FMT_CBOR)) {
            json = nlohmann::json::from_cbor(buffer);
        }
        return json;
    }

    template<typename Function>
    void reg_handle(const std::string& name, Function f) {
        auto h = hash(name);
        invokes_[h] = { name, [f](const std::shared_ptr<connection>& conn, const std::string& func_name, msg_id_t id, const std::string& buffer) {
            return invoke_callback<Function, std::nullptr_t>(f, nullptr, conn, func_name, id, buffer);
        }
                      };
    }

    template<typename Function, typename SelfClass>
    void reg_handle(const std::string& name, Function f, SelfClass* self) {
        auto h = hash(name);
        invokes_[h] = { name, [f, self](const std::shared_ptr<connection>& conn, const std::string& func_name, msg_id_t id, const std::string& buffer) {
            return invoke_callback<Function, SelfClass>(f, self, conn, func_name, id, buffer);
        }
                      };
    }

    template<typename Function>
    void reg_handle(const std::string& name, Function f, std::weak_ptr<asio::io_context> wio) {
        auto h = hash(name);
        invokes_[h] = { name, [f, wio](const std::shared_ptr<connection>& conn, const std::string& func_name, msg_id_t id, const std::string& buffer) {
			auto io = wio.lock();
			if (io == nullptr) {
				LOG_WARN("call invoke after shutdown: {}({})", func_name, id);
				return false;
			}
            io->dispatch([f, conn, func_name, id, buffer = std::move(buffer)]() {
                invoke_callback<Function, std::nullptr_t>(f, nullptr, conn, func_name, id, buffer);
            });
            return true;
        }
                      };
    }

    template<typename Function, typename SelfClass>
    void reg_handle(const std::string& name, Function f, SelfClass* self, std::weak_ptr<asio::io_context> wio) {
        auto h = hash(name);
        invokes_[h] = { name, [f, self, wio](const std::shared_ptr<connection>& conn, const std::string& func_name, msg_id_t id, const std::string& buffer) {
			auto io = wio.lock();
			if (io == nullptr) {
				LOG_WARN("call invoke after shutdown: {}({})", func_name, id);
				return false;
			}
			io->dispatch([f, self, conn, func_name, id, buffer = std::move(buffer)]() {
                invoke_callback<Function, SelfClass>(f, self, conn, func_name, id, buffer);
            });
            return true;
        }
                      };
    }

    std::string query_msg_name(uint64_t msg_id) {
        auto iter = invokes_.find(msg_id);
        return iter == invokes_.end() ? "unknow" : iter->second.first;
    }

    template<typename Connection>
    void route_request(const std::shared_ptr<Connection>& conn,
                       msg_id_t id,
                       const std::string& buffer) {
		bool is_no_response = id.msg_type & (1 << uint16_t(msg_type_bits::MSG_IS_NO_RESPONSE));
        auto on_exception = [is_no_response](const std::shared_ptr<Connection>& conn, msg_id_t& id, int e) {
            if (e != 0)	{
                LOG_ERROR("rpc invoke, {} throw exception: {}", id, e);
                if (is_no_response) conn->response(id, e, "Internal Exception", nullptr);
            }
            // else (e == 0) {} // just dont want response
        };
        try {
            auto iter = invokes_.find(id.msg_id);
            if (iter == invokes_.end()) {
                LOG_ERROR("rpc invoke not found, {} ", id);
                throw not_implemented;
            }
            if (!iter->second.second(conn, iter->second.first, id, buffer)) {
                LOG_ERROR("rpc invoke{}({}) function failed, return is false  ", iter->second.first, id);
                throw internal_server_error;
            }
        } catch (int e) {
            on_exception(conn, id, e);
        } catch (status e) {
            on_exception(conn, id, e);
        } catch (const std::system_error& e) {
            LOG_ERROR("rpc invoke, {} throw exception: {}, {}", id, e.code().value(), e.what());
            if (is_no_response) conn->response(id, e.code().value(), e.what(), nullptr);
        } catch (const std::exception& e) {
            LOG_ERROR("rpc invoke, {} throw exception: {}", id, e.what());
            if (is_no_response) conn->response(id, status::internal_server_error, e.what(), nullptr);
        } catch (...) {
            LOG_ERROR("rpc invoke, {} throw unknow exception", id);
            if (is_no_response) conn->response(id, status::internal_server_error, "Internal Server Error", nullptr);
        }
    }

    // copy from std::hash<string>, but always return 64bit value && the max bit is 1
    static uint64_t hash(const std::string_view& key) {
        constexpr static uint64_t _FNV_offset_basis = 14695981039346656037ULL;
        constexpr static uint64_t _FNV_prime = 1099511628211ULL;
        auto _Val = _FNV_offset_basis;

        for (const auto& c : key) {
            _Val ^= static_cast<uint64_t>(c);
            _Val *= _FNV_prime;
        }
        auto ret =  (_Val | (1ull << 63));
		// LOG_TRACE("{}'s hash: {}", key, _Val);
		return ret;
    }

  private:
    template<int I, typename JsonType, typename TupleType>
    static void parameter_append_op(JsonType& json, TupleType& tp) {
        if (I >= json.size()) {
            json.emplace_back(std::get<I>(tp));
        }
    }

    template<typename JsonType, typename TupleType, typename T,  T... I>
    static void parameter_extend(JsonType& json, TupleType& tp, std::integer_sequence<T, I...> int_seq) {
        ((parameter_append_op<I>(json, tp)), ...);
    }


  private:
    using invoke_type = std::function<bool (const std::shared_ptr<mrpc::connection>&,
											const std::string&,
                                            msg_id_t,
                                            const std::string&)>;
    std::unordered_map<uint64_t, std::pair<std::string, invoke_type>> invokes_; // msg_id, <msg_name, invoke_func>

    std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<connection>> conns_;
};
} // namespace mrpc

#endif // MRPC_ROUTER_HPP