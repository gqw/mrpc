#ifndef MRC_COROUTINE_HPP
#define MRC_COROUTINE_HPP

#pragma once

#ifdef _USE_COROUTINE

#ifdef WIN32
#   include <experimental/coroutine>
    namespace stdcoro = std::experimental;
#else
#   include <coroutine>
    namespace stdcoro = std;
#endif
#include <exception>

namespace mrpc {

template<typename T> class task_promise;
template<typename T> class req_result;


template<typename T>
struct task_awaitable {
    task_awaitable() {}
    task_awaitable(std::function<void(stdcoro::coroutine_handle<>)> suspend_callback,
                   std::function<req_result<T>()> resume_callback)
        : suspend_callback_(suspend_callback)
        , resume_callback_(resume_callback) {

    }

    void set_suspend_callback(std::function<void(stdcoro::coroutine_handle<>)> suspend_callback) {
        suspend_callback_ = suspend_callback;
    }
    void set_resume_callback(std::function<req_result<T>()> resume_callback) {
        resume_callback_ = resume_callback;
    }

    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(stdcoro::coroutine_handle<> coroutine) {
        suspend_callback_(coroutine);
        return;
    }

    decltype(auto) await_resume() noexcept {
        return resume_callback_();
    }

  private:
    std::function<void(stdcoro::coroutine_handle<>)> suspend_callback_;
    std::function<req_result<T>()> resume_callback_;
};

template<typename T = void>
class task {
  public:
    using promise_type = task_promise<T>;
    using value_type = T;
    using cptr = const std::shared_ptr<T>&;

  public:
    task(stdcoro::coroutine_handle<promise_type> h) {

    }

    auto operator co_await() const & noexcept {
        return task_awaitable<T>{};
    }

  private:
    stdcoro::coroutine_handle<promise_type> coroutine_;
};

class task_promise_base {
  public:
    auto initial_suspend() {
        return stdcoro::suspend_never{};
    }

    auto final_suspend() {
        return stdcoro::suspend_never{};
    }

    void unhandled_exception() {
        m_exception = std::current_exception();
    }

  private:
    std::exception_ptr m_exception;
};

struct get_promise_t {};
constexpr get_promise_t get_promise = {};

template<typename T>
class task_promise final : public task_promise_base {
  public:

  public:
    task<T> get_return_object() noexcept {
        return task<T>(stdcoro::coroutine_handle<task_promise>::from_promise(*this));
    }

    void return_value(T value) {
        // LOG_DEBUG("return_value: {}", value);
    }

};


template<>
class task_promise<void> final : public task_promise_base {
  public:

  public:
    task<void> get_return_object() noexcept {
        return task<void>(stdcoro::coroutine_handle<task_promise>::from_promise(*this));
    }

    void return_void() {
        //LOG_DEBUG("return void");
    }

};

} // namespace mrpc

#endif // _USE_COROUTINE

#endif // MRC_COROUTINE_HPP