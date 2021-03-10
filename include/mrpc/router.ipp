#ifndef MRPC_ROUTER_IPP
#define MRPC_ROUTER_IPP

#pragma once

#include "router.hpp"

template<typename Function, typename SelfClass>
bool mrpc::router::invoke_callback(Function f, SelfClass* self,
                                const std::shared_ptr<connection>& conn,
								const std::string& func_name,
                                msg_id_t id,
                                const std::string& buffer) {

    using noclass_tp_type = std::tuple<const std::shared_ptr<connection>&>;
    using class_tp_type = std::tuple<SelfClass*, const std::shared_ptr<connection>&>;
    using tp_header_type = std::conditional_t<std::is_null_pointer_v<SelfClass>, noclass_tp_type, class_tp_type>;
    bool is_no_response = id.msg_type & (1 << MSG_IS_NO_RESPONSE);

    if (id.msg_type & (1 << MSG_FMT_RAW)) {
        if constexpr (function_traits<Function>::argc == 2) {
            nlohmann::json jret;
            using first_p_type = std::tuple_element_t<0, typename function_traits<Function>::args_tuple>;
            using second_p_type = std::tuple_element_t<1, typename function_traits<Function>::args_tuple>;
            // 检查前两个参数类型是否为connection 和 string
            if constexpr (std::is_same_v<first_p_type, std::shared_ptr<connection>> && std::is_same_v<second_p_type, std::string>) {
                if constexpr (std::is_void_v<function_traits<Function>::return_type>) {
                    f(conn, buffer);
                } else {
                    jret = f(conn, buffer);
                }
                return !is_no_response ? conn->response(id, status::ok, "ok", jret) : true;
            }
        }
        return !is_no_response ? conn->response(id, status::internal_server_error, "internal error", nullptr) : true;
    }

    typename function_traits<Function>::args_tuple args;
    nlohmann::json json = decode(id.msg_type, buffer);
    if (json.size() < function_traits<Function>::argc) {
        // 为了兼容性考虑，函数参数大于接受的参数，自动补全缺失的参数
        LOG_WARN("msg: {} parameter not full", id);
        parameter_extend(json, args, std::make_index_sequence<function_traits<Function>::argc> {});
    }
    LOG_TRACE("recv msg: {}({}), content: {}", func_name, id, json.dump());
    nlohmann::from_json(json, args);

    std::unique_ptr<tp_header_type> tp;
    if constexpr (std::is_null_pointer_v<SelfClass>) {
        tp = std::make_unique<tp_header_type>(conn);
    } else {
        tp = std::make_unique<tp_header_type>(self, conn);
    }
    nlohmann::json jret;
    if constexpr (std::is_void_v<typename function_traits<Function>::return_type>) {
        std::apply(f, std::tuple_cat(std::move(*tp), std::move(args)));
    } else {
        jret = std::apply(f, std::tuple_cat(std::move(*tp), std::move(args)));
    }
    return !is_no_response ? conn->response(id, status::ok, "ok", jret) : true;
}

#endif // MRPC_ROUTER_IPP