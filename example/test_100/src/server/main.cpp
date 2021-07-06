#include <logger.hpp>
#include <mrpc/server.hpp>

using namespace mrpc;
int main() {
    wlog::logger::get().init("logs/" PROJECT_NAME ".log");
    auto& server = server::get();
    server.router().reg_handle("test_add", [](connection::cptr conn, int i, int j) {
        LOG_DEBUG("recv test add: {} + {}", i, j);
        return i + j;
    });
    server.router().reg_handle("query_funcname", [](connection::cptr conn, uint64_t msg_id) {
        auto funcname = conn->router().query_msg_name(msg_id);
        LOG_DEBUG("remote query message name: {}-{}", funcname, msg_id);
        return std::make_tuple(msg_id, funcname);
    });

    server.router().set_exception_callback([](connection::cptr conn, int error,
         msg_id_t id, const std::string& buffer){
             if (error == mrpc::not_implemented) {
                conn->async_call([](uint32_t err_code, const std::string& err_msg, const nlohmann::json& ret){
                    LOG_DEBUG("query_funcname response: {}", ret.dump());
                }, "query_funcname", id.msg_id);
             }
    });

    server.run();
    server.do_accept("0.0.0.0", 3333);
    server.wait_shutdown();
    wlog::logger::get().shutdown();
    return 0;
}