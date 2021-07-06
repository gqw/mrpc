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

前面说过希望大家能够通过代码学习能够开发适合自己需求的RPC，所以我想在这里尽量细致的介绍这些代码。因为我希望即使是新手也能看得明白，所以可能会显得有些啰嗦，如果你自己已经能看明白，那就直接跳过吧。

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

RPC在client-server和分布式系统中是一种非常方便、有用的技术。现在的RPC框架也不计其数，有谷歌的gRpc, 百度的bRpc, Facebook的Thrift..., 但是这些RPC库，在很大程度上功能与实现已经远远超出RPC的范围了，例如，bRpc不仅包含了基本的序列化/反序列化，网络IO, 甚至包含了负载，可视化工具等，想在短期内掌握和使用这些库并不容易。mRPC需要做的就是化繁为简，只关心RPC最核心的部分——“方法调用”。尽量做到看一眼会用，看一会心里有底，用两天就能触类旁通。如果达到这种效果，我想如果遇到问题处理起来也会游刃有余了，说明代码已经真正属于你的了。。

既然我们现在关心的重点在于实现远程方法调用，那么事情就会变得简单多了，我们只要处理好io和序列化两件事情就行了。io我们使用了asio, 序列化使用了nlohmann/json，有了这两个把屠龙刀和倚天剑，才有了我们如此简洁的RPC实现。

<!-- ### asio



### 序列化

RPC的另一重要组成部分便是序列化, 在mrpc中我们使用担任nlohmann/json序列化和反序列化工作。虽然叫json但是这个库可不仅仅为json服务，它支持多种主流序列化格式，如：json, msgpack, bjson等等， 甚至还支持自定义扩展。实际上mrpc使用的nlohmann/json从严格意义上来说并不能算是完整的序列化和反序列话，因为它不能直接将数据和用户自定义对象进行互转，但是一方面适合工作中的需求，另一方面实在不喜欢侵入式的序列化和反序列化代码（每个需要序列化的结构需要额外的宏定义），还有一点nlohmann/json效率真的很好呀。 -->


### client, server 代码解读

client, server代码比较简单，需要说明的是mrpc中server采用的是每个IO一个线程的多IO多线程模型。另外与rest_rpc不同的是，rest_rpc只能server注册服务，也就是只能客户端调用服务端方法，它的远程方法回调是直接放在server类里管理的。为了实现客户端和服务端双向调用mrpc对注册进行了抽取封装，独立出一个router类。所以在client和server里面都有一个router对象。客户端和服务端都可以通过：

```cpp
    server::get().router().reg_handle("xxx", ...);
```
或
```cpp
    client::get().router().reg_handle("xxx", ...);
```
进行服务注册。

#### 知识点: C++单例

client, server类都是单例实现，c++11以前实现单例还是挺麻烦的，需要考虑如何禁止继承、禁止拷贝复制、线程安全、加锁效率、使用“双检锁”技巧等。自从c++11以后事情就变得简单多了。

1. 禁止继承，我们有 `final` 关键字。
2. 禁止拷贝，我们有 `delete` 关键字，可以禁止编译器生成不需要的构造函数。
3. 线程安全，从C++11以后标准规定静态变量在首次初始化时，如果发生竞争，需要等待其初始化完成，也就是一个线程初始化静态变量如果与其它线程发生竞争，需要停下来等待其它线程初始化完成。这样我们就可以使用C++11写出非常简洁的单例代码：
```cpp
    static single& instance() {
        static single s;
        return s;
    }
```

#### 知识点: 友元类使用

一般来说使用友元是一种破坏封装的行为，因为它无视了面向对象的一些隐藏规则。通过友元我们可以访问类的保护和私有变量和方法。但是有时我们在编写接口时会遇到一种矛盾的处境，我们向对外保持接口简洁的同时又能够被系统内部的其他类和方法访问到内部的信息。因为语言没有提供向特定类和方法暴露内部特定信息的方法（如只向某个类暴露自己的某个成员变量），所以采用友元是一种便捷的方法。


#### 知识点: asio和线程模型

asio通常被认为是一个高效的网络库，虽然也的确有较大机会作为C++的网络库写进标准，但是他的能力远不止作为网络库使用，文件操作，进程间通信，定时器都能胜任，可以参考我写的使用stream_handle进行pipe读写的例子。所以想要实现进程间RPC使用asio也是很容易的。

认识的人中好多对asio具有抵触情绪，主要有两个原因：

1. 认为asio太重。有可能是因为早期asio总是和boost绑定，其实从支持c++11后，asio 已经可以脱离boost独立部署了。并且独立后的asio是只有头文件依赖的，非常容易整合进自己的项目中。而且asio极有可能并入c++标准，这样整合成本更低。
2. asio 封装太重，理解起来太难。其实常用的代码也就那些，看多了也就熟悉了，asio无非是对系统调用的封装，例如windows平台下就是对iocp(完成端口)的封装，先从封装的上层调用开始层层封装，将调用成OVERLAPPED对象指针，传入相应的系统涵数中，收到完成消息后在层层解封找到用户的回调函数后调用。

说到asio我们必须了解下两种I/O多路复用模式：Reactor（反应堆模式）和Proactor（前摄器模式）。具体的概念不再展开，这里只说下我对这两种模式的理解：

1. Reactor 和 Proactor 是系统对I/O事件的不同分发方式。
2. Reactor 是通过轮询方式不断向系统查询I/O状态，如果系统准备就绪则需要应用自己拷贝需要的数据。典型的代表有libevent/libev/libuv/Redis等库。
3. Proactor 在调用系统函数前事先准备好数据缓冲，等系统读取到数据并拷贝到准备好的缓冲上后会主动通知调用者。 典型的代表有IOCP/asio等库。
4. 在Linux系统上asio模拟了IOCP的行为。
5. 本质上Reactor是I/O同步调用，Proactor才是异步调用。从理论上Proactor会比Reactor具有更好的性能，因为少了从内核态向用户态拷贝数据的过程
6. 正如asio文档所说，Proactor会比Reactor更耗内存，因为Proactor需要事先准备好内存，在系统返回结果前会一直占用，但Reactor只需要在有结果前才分配内存。

asio线程模型：

asio 将所有的I/O调用封装在了io_context类里。如果做个类比，可以把整个进程比作工厂，io_context就是里面的车间，而线程便是车间里的工人。进程负责接活，然后将任务分配给车间，但是真正干活的车间里的工人。按照工厂规模我们可以分成以下几种：

1. 小作坊： 工厂只有一个车间，车间里只有一个工人，即“单I/O单线程”。这种模式效率是非常低的，因为即使工厂老板有钱雇佣了多个工人（多核CPU）,但是真正能干活的人只有一人。好处是做事有条不紊，不会发生争抢资源的情况。
   ```cpp
   // 单I/O单线程 代码实现
   mrpc::server::get().run(1, 1);
   ```
2. 小工厂： 一个工厂只有一个车间，但是老板雇佣了多个工人一起干活，及“单I/O多线程”。这种模式效率是比小作坊好多了。但是也带来了新问题，一些的资源需要共享使用，这就导致了工人会争抢工具导致产品不合格。老板为了解决这个问题，想了个办法把共享的资源放到上锁的仓库中，需要使用工具必须向老板申请钥匙开锁（线程锁）才能使用，用完归还老板，这样就避免了共享资源的竞争。但是同样又引入了另外一个问题：每次申请和归还锁是件繁琐的事，特别是工作繁忙时，可能申请和归还锁的操作的时间远大于使用工具的时间。
   ```cpp
   // 单I/O多线程 代码实现
   mrpc::server::get().run(1, n); // n > 0
   ```
3. 大型工厂：大型工厂像是小工厂的翻倍，由多个组成车间组成，每个车间多名工人，即“多I/O多线程”。虽然大型工厂的工作负载增加了，但是同样面临小工厂遇到的问题，申请释放锁的开销太大。
    ```cpp
   // 多I/O多线程 代码实现
   mrpc::server::get().run(m, n); // m > 0 && n > 0
   ```
4. 土豪工厂：虽然土豪工厂和大型工厂一样都是有多个车间和多个工人组成，但是土豪比较任性给每个车间只配备一名工人，每个车间都配备专用的工具。这样就解决了资源共享问题，而土豪解决高负载的方法也很简单，增加车间便可以了。这便是“每I/O一个线程”方案。
    ```cpp
   // 每I/O一个线程 代码实现
   mrpc::server::get().run(m, 1); // m > 0
   ```
5. 临时工：这种工厂老板比较抠门，虽然整个工厂虽然有多个车间，但是工人只有一个，即“多I/O共享线程”。因为只有一个工人（线程）所以不会发生竞争的情形，也就不需要加锁保护。这种方案比较适合工作负载比较低的情形。
    ```cpp
   // 多I/O共享线程 代码实现
   mrpc::server::get().run(m, 0); // m > 0
   do {
       mrpc::server::get().run_once();
   } while(true)
   ```

从上面的代码我们可以看到mrpc是通过run参数io_count和thread_per_io进行切换的。io_count控制I/O对象数量，thread_per_io控制每个I/O对象线程数量。特殊的在“多I/O共享线程”模式中，没有为I/O对象分配线程，而是由用户通过循环调用run_once方法驱动消息分发的。

#### 知识点: asio work 功能

仔细看client和server的代码，可以看到我们使用创建每个I/O对象分别又创建了一个work对象。那么这个work是什么呢，干什么用的呢？
asio的io_context对象调用run的时候如果发现没有任务需要自己做了，就会自动退出。但是这样不行的，应为我们在run之前还没有分配任务呢，这是io_context就退出，后面的任务就没人执行了。还拿前面工厂的例子打个比方，工厂车间的负责任刚上班发现老板没有分配任务就直接宣布下班了，后面的任务谁做呢？所以老板又想了了个办法，给每个车间都提了个不可能完成但又非常简单的任务（比如等公司发大财了你们才算完成任务），这样既不会消耗工人的时间有能让车间永远下不了班。而这里的work就是那个老板布置的任务。io_context一直在等待work完成，但其实work是永远不会完成的。除非得到老板的指示“算了，不用等了（调用io_context的stop方法）”，才能结束。

### connection 代码解读

无论是client还是server都会创建connection对象，而且它们创建的connection对象在地位和功能上是完全相同的，它们唯一的区别就是创建的方式不同，client通过connect连接创建，server通过accept回调创建，一旦创建完成它们就没有区别了，都可以发起方法调用也可以响应调用。这里的代码与rest_rpc有较大的不同，rest_rpc客户端调用服务端的方法，不能反过来。但是实际的工程中客户端、服务端的身份有时很难明确的界定，特别是服务器之间的连接。

每个connection都有个socket对象，而socket对象又是与io_context绑定的， 这里的socket有点像前面工厂车间门面店的意思，由它来对外宣布自己的经营范围（TCP读写，UDP读写或者是定时器）、承接任务，但是真正做事的仍旧是工厂里的车间(io_context)。当然每个车间是可以有多个门面的，也就是可以做不同类型的任务。

通常connection创建后都会调用其start方法开始进入循环读写模式。那就从读消息开始讲解下代码。

#### 知识点: 面向流的TCP协议

### router 代码解读
### 协程原理与coroutine代码实现解读



## 未整理

write_size_

https://www.cnblogs.com/xiaolincoding/p/11437231.html
https://stackoverflow.com/questions/1661529/is-meyers-implementation-of-the-singleton-pattern-thread-safe
https://eel.is/c++draft/stmt.dcl