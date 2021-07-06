// #include <logger.hpp>
// #include <mrpc/server.hpp>
// using namespace mrpc;
// #include "test.h"
#include <iostream>
#include <arpa/inet.h>

# define htonll(x) (uint64_t(htonl((x))) << 32 | htonl((x) >> 32))
# define ntohll(x) (uint64_t(ntohl((x))) << 32 | ntohl((x) >> 32))

#define PRINT_FATAL_Test(msg,...)


class Test {
public:
    Test() {
        std::cout << "Test" << std::endl;
    }
    ~Test() {
        std::cout << "~Test" << std::endl;
    }

};

int main() {
	// static_assert(false, "in main");
    uint32_t i = 0x01020304;
    auto j = htonl(i);
    auto l = ntohl(j);

    std::cout << std::hex << i << " " << j << " " << l << std::endl;

    uint64_t x = 0x0102030405060708;
    auto d = (uint64_t(htonl((x))) << 32 | htonl((x) >> 32));
    auto f = (uint64_t(ntohl((d))) << 32 | ntohl((d) >> 32));
    std::cout << std::hex << x << " " << d << " " << f << std::endl;

    auto y = htonll(x);
    auto z = ntohll(y);
    std::cout << std::hex << x << " " << y << " " << z << std::endl;

    PRINT_FATAL_Test("%d", Test());
    return 0;
	// test();

    // wlog::logger::get().init("logs/" PROJECT_NAME ".log");
    // auto& server = server::get();
    // server.router().reg_handle("test_add", [](connection::cptr conn, int i, int j) {
    //     LOG_DEBUG("recv test add: {} + {}", i, j);
    //     return i + j;
    // });

    // server.run();
    // server.do_accept("0.0.0.0", 3333);
    // server.wait_shutdown();
    // wlog::logger::get().shutdown();
    return 0;
}