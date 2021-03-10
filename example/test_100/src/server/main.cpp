#include <logger.hpp>
#include <mrpc/server.hpp>

using namespace mrpc;
int main() {
    wlog::logger::get().init("logs/" PROJECT_NAME ".log");
    auto& server = server::get();
    server.do_accept("0.0.0.0", 3333);

    server.router().reg_handle("test_add", [](connection::cptr conn, int i, int j) {
        LOG_DEBUG("recv test add: {} + {}", i, j);
        return i + j;
    });

    server.run();
    server.shutdown();
    wlog::logger::get().shutdown();
    return 0;
}