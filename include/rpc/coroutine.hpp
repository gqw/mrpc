#pragma once

#ifdef WIN32
#   include <experimental/coroutine>
#else
#   include <coroutine>
#endif
#include <exception>

namespace mrpc {

#ifdef _USE_COROUTINE

template<typename T> class task_promise;
template<typename T> class req_result;
template<typename T = void>
class task {
  public:
    using promise_type = task_promise<T>;
    using value_type = T;
    using cptr = const std::shared_ptr<T>&;

  public:
    task(std::experimental::coroutine_handle<promise_type> h) {

    }

    auto operator co_await() const & noexcept {
        return task_awaitable{};
    }

  private:
    std::experimental::coroutine_handle<promise_type> coroutine_;
};

template<typename T>
struct task_awaitable {
    task_awaitable() {}
    task_awaitable(std::function<void(std::experimental::coroutine_handle<>)> suspend_callback,
                   std::function<req_result<T>()> resume_callback)
        : suspend_callback_(suspend_callback)
        , resume_callback_(resume_callback) {

    }

    void set_suspend_callback(std::function<void(std::experimental::coroutine_handle<>)> suspend_callback) {
        suspend_callback_ = suspend_callback;
    }
    void set_resume_callback(std::function<req_result<T>()> resume_callback) {
        resume_callback_ = resume_callback;
    }

    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::experimental::coroutine_handle<> coroutine) {
        suspend_callback_(coroutine);
        return;
    }

    decltype(auto) await_resume() noexcept {
        return resume_callback_();
    }

  private:
    std::function<void(std::experimental::coroutine_handle<>)> suspend_callback_;
    std::function<req_result<T>()> resume_callback_;
};



class task_promise_base {
  public:
    auto initial_suspend() {
        return std::experimental::suspend_never{};
    }

    auto final_suspend() {
        return std::experimental::suspend_never{};
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
        return task<T>(std::experimental::coroutine_handle<task_promise>::from_promise(*this));
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
        return task<void>(std::experimental::coroutine_handle<task_promise>::from_promise(*this));
    }

    void return_void() {
        //LOG_DEBUG("return void");
    }

};

#endif // _USE_COROUTINE
} // namespace mrpc