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

## 原理介绍

前面说过希望大家能够通过代码学习能够开发适合自己需求的RPC，所以我想在这里详细的介绍下这些代码。因为我希望即使是新手也能看得明白，所以可能会显得有些啰嗦，如果你自己已经能看明白，那就直接跳过吧。

我们先来看下代码结构吧：
```
include
└── rpc
    ├── client.hpp
    ├── connection.hpp
    ├── coroutine.hpp
    ├── router.hpp
    └── server.hpp
```
全部代码只有这5个头文件，其中client.hpp和server.hpp是标准的asio用法，使用过asio的同学应该很熟悉。另外如果对协程调用不感兴趣coroutine.hpp也可以忽略不看了，那么剩下就只剩下connection.hpp和router.hpp这两个文件了。两个文件总计也没有超过1000行代码，所以从心里上不要害怕，告诉自己应该能容易搞定。

下面我们展开来讨论下，为了照顾新手有些知识点会进行扩展讲解，如果已经了解的跳过即可。

### rpc 解释

远程方法调用 (remote procedure call) 维基百科中的定义为：不用过多的代码就能像调用本地方法一样调用不同地址空间的方法。这里的不同的“地址空间”描述的还是比较准确的。虽然我们通常认为它代表的是不同的机器，但是同一机器，同一进程也是可以进行远程方法调用的，例如不同的lua state之间的调用，只要进行了地址隔离。如下图调用流程图所示：

![mrpc_call_1.png](https://i.loli.net/2021/03/05/xBUG3uhmvtKwIgH.png)

可以看出RPC调用其实非常简单，从客户端发起调用经过信息打包发送至服务端，服务端收到消息后对其进行解包，解包后将客户端调用信息传递给实际处理函数。最后服务端再经过同样的流程将结果反向发送给客户端。这与不使用RPC有什么区别呢？主要在于前面定义中说的“像”这个关键字。如何使远程调用更像本地调用，使调用放感知不到远程服务器的存在，就要看各个RPC实现者的功夫了。这也是mRPC和rest_rpc的关键，也是现代C++的魅力所在。

RPC在client-server和分布式系统中是一种非常方便、有用的技术。现在的RPC框架也不计其数，有谷歌的gRpc, 百度的bRpc, Facebook的Thrift..., 但是这些RPC库，在很大程度上功能与实现已经远远超出RPC的范围了，例如，bRpc不仅包含了基本的序列化/反序列化，网络IO, 甚至包含了负载，可视化工具等，想在短期内掌握和使用这些库并不容易。mRPC需要做的就是化繁为简，只关心RPC最核心的部分——“方法调用”。尽量做到看一眼会用，看一会心里有底，用两天后会说“就这？”。如果达到这种效果，我想如果遇到问题处理起来也会游刃有余了，说明这里面写的和代码已经真正属于你的了。。

既然我们现在关心的重点在于实现远程方法调用，那么事情就会变得简单多了，我们只要处理好io和序列化两件事情就行了。io我们使用了asio, 序列化使用了nlohmann/json，有了这两个把屠龙刀和倚天剑，才有了我们如此简洁的RPC实现。

### asio 基础

asio通常被认为是一个高效的网络库，虽然也的确有较大机会作为C++的网络库写进标准，但是他的能力远不止作为网络库使用，文件操作，进程间通信都能胜任，可以参考我写的使用stream_handle进行pipe读写的例子。所以想要实现进程间RPC使用asio也是很容易的。

认识的人中好多对asio具有抵触情绪，主要有两个原因：

1. 认为asio太重。有可能是因为早期asio总是和boost绑定，其实从支持c++11后，asio 已经可以脱离boost独立部署了。并且独立后的asio是只有头文件依赖的，非常容易整合进自己的项目中。而且asio极有可能并入c++标准，这样整合成本更低。
2. asio 封装太重，理解起来太难。其实常用的代码也就那些，看多了也就熟悉了，asio无非是对系统调用的封装，例如windows平台下就是对iocp(完成端口)的封装，先从封装的上层调用开始层层封装，将调用信息封装成OVERLAPPED对象指针，传入相应的系统涵数中，收到完成消息后在层层解封找到用户的回调函数后调用。

### 序列化

RPC的另一重要组成部分便是序列化

### connection 代码解读
### router 代码解读
### 协程原理与coroutine代码实现解读



## 未整理

write_size_