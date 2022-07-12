# mRPC

## 介绍

mRPC 是一个支持C++协程调用的轻量级、现代化的RPC库，它基于rest_rpc改造而来。与rest_rpc一样，它短小精悍所有核心代码不足千行，但却具备rpc的完整功能。得益于现代c++的高级功能，使得调用远程代码就像调用本地代码一样简单。与rest_rpc比较主要做了如下的修改：

    1. 支持协程调用
    2. 服务端客户端地位平等，支持双向调用
    3. 去掉调用过程中的异常处理（不喜欢每次调用都需要try...catch）
    4. 自带支持更多的协议（RAW, JSON, BJSON, UBJSON, MSGPACK, CBOR）
    5. 没有历史包袱，使用新的语言特性对代码做了优化
    6. 支持日志打印

既然有了rest_rpc为什么还要重新造轮子呢？用别人的东西总有不顺手的地方比如rest_rpc中服务端不能直接调用客户端的方法，再比如现有工程中主要用nlohmann/json不想引入msgpack解析库，等等。但是最重要的是rest_rpc实在是太小巧了，千八百行的代码稍微花点时间便能理解其精要，重新开发个符合自己项目需求的RPC也不需要消耗太多的精力。所以本来都没打算给自己的这个库起名字，但为了搜索查找方便还是起了个叫mrpc, 为了方便记忆你可以把这里的 “m” 理解成micro或modern。其实我不太希望你直接用这个库，而是应该看下代码，了解原理后开发个符合自己需求的RPC（毕竟全部代码还不足千行）, 当然如果发现它完全满足自己的需求那就直接用吧:)。

代码虽少，但是绝对有值得学习的地方，特别是对现代c++知识的理解。这里要非常感谢rest_rpc的作者，通过对rest_rpc代码的阅读使我学习到了许多知识。

## 使用方法

mRPC和其依赖的第三方库都是是"Header only"的，所以只需要clone或下载include/rpc目录和依赖的third目录，然后如果是服务端只需添加：
```cpp
#include <mrpc/sever.hpp>
```
客户端添加：
```cpp
#include <mrpc/client.hpp>
```
就可以了。


### 服务注册

```cpp
// server
server.router().reg_handle("test_add", [](connection::cptr conn, int i, int j) {
    LOG_DEBUG("recv test add: {} + {}", i, j);
    return i + j;
});
```

### 同步调用
---

```cpp
// client
auto ret = conn->call<uint32_t>("test_add", 11, 12);
if (ret.error_code() == mrpc::ok) {
    std::cout << "return: " << ret.value() << std::endl;
} else {
    std::cout << "return error: " << ret.error_msg() << std::endl;
}
```

### 异步调用
---

```cpp
// client
conn->async_call([](uint32_t err_code, const std::string& err_msg, const nlohmann::json& ret){
    if (err_code == mrpc::ok) {
        std::cout << "return: " << ret.get<uint32_t>() << std::endl;
    } else {
        std::cout << "return error: " << err_msg << std::endl;
    }
}, "test_add", 11, 12);
```

### 协程调用
---

```cpp
// client
task<uint32_t> test_coro(connection::cptr conn) {
    auto ret = co_await conn->coro_call<int>("test_add", 1, 1);  // 1+1
    ret = co_await conn->coro_call<int>("test_add", ret.value(), ret.value()); // 2+2
    ret = co_await conn->coro_call<int>("test_add", ret.value(), ret.value()); // 4+4
    ret = co_await conn->coro_call<int>("test_add", ret.value(), ret.value()); // 8+8
    co_return ret.value(); // 16
}
```

## 依赖库

    1. asio 1.18
    2. nlohmann/json
    3. spdlog

