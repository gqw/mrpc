#include <rpc/server.hpp>

int main() {
    auto& server = rpc::server::get();
    server.do_accept("0.0.0.0", 3333);
    server.run();
    server.stop();
    return 0;
}