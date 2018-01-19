#include <experimental/thread_pool>
#include <cassert>
#include <chrono>
#include <iostream>

namespace execution = std::experimental::execution;
using std::experimental::static_thread_pool;

namespace custom_properties
{
  struct nonblocking_oneway_delay
  {
    std::chrono::steady_clock::duration duration;
  };

  // Default hint implementation creates an adapter, but only when require() is used.

  template <class InnerExecutor>
  class nonblocking_oneway_delay_executor
  {
    std::chrono::steady_clock::duration delay_;
    InnerExecutor inner_ex_;
    bool is_never_blocking_;

    template <class T> static auto inner_declval() -> decltype(std::declval<InnerExecutor>());

    template <class Executor>
    static constexpr auto has_never_blocking_property(const Executor* ex)
      -> decltype(execution::require(*ex, execution::never_blocking) == *ex)
    {
      return execution::require(*ex, execution::never_blocking) == *ex;
    }

    static constexpr bool has_never_blocking_property(...)
    {
      return false;
    }

  public:
    nonblocking_oneway_delay_executor(std::chrono::steady_clock::duration delay, const InnerExecutor& ex)
      : delay_(delay), inner_ex_(ex), is_never_blocking_((has_never_blocking_property)(&ex)) {}

    nonblocking_oneway_delay_executor(std::chrono::steady_clock::duration delay, const InnerExecutor& ex, bool nb)
      : delay_(delay), inner_ex_(ex), is_never_blocking_(nb) {}

    // Intercept require requests for a delay.
    nonblocking_oneway_delay_executor require(custom_properties::nonblocking_oneway_delay d) const
      { return { d.duration, inner_ex_, is_never_blocking_ }; }

    // Intercept require requests for blocking properties.
    template <class Property> auto require(const execution::always_blocking_t& p) const &
      -> nonblocking_oneway_delay_executor<execution::require_member_result_t<InnerExecutor, Property>>
        { return { delay_, inner_ex_.require(p), false }; }
    template <class Property> auto require(const execution::always_blocking_t& p) &&
      -> nonblocking_oneway_delay_executor<execution::require_member_result_t<InnerExecutor&&, Property>>
        { return { delay_, inner_ex_.require(p), false }; }
    template <class Property> auto require(const execution::possibly_blocking_t& p) const &
      -> nonblocking_oneway_delay_executor<execution::require_member_result_t<InnerExecutor, Property>>
        { return { delay_, inner_ex_.require(p) }; }
    template <class Property> auto require(const execution::possibly_blocking_t& p) &&
      -> nonblocking_oneway_delay_executor<execution::require_member_result_t<InnerExecutor&&, Property>>
        { return { delay_, inner_ex_.require(p) }; }
    template <class Property> auto require(const execution::never_blocking_t& p) const &
      -> nonblocking_oneway_delay_executor<execution::require_member_result_t<InnerExecutor, Property>>
        { return { delay_, inner_ex_.require(p), true }; }
    template <class Property> auto require(const execution::never_blocking_t& p) &&
      -> nonblocking_oneway_delay_executor<execution::require_member_result_t<InnerExecutor&&, Property>>
        { return { delay_, inner_ex_.require(p), true }; }

    // Forward other kinds of require to the inner executor.
    template <class Property> auto require(const Property& p) const &
      -> nonblocking_oneway_delay_executor<execution::require_member_result_t<InnerExecutor, Property>>
        { return { delay_, inner_ex_.require(p), std::is_same<Property, execution::never_blocking_t>::value ? true : is_never_blocking_ }; }
    template <class Property> auto require(const Property& p) &&
      -> nonblocking_oneway_delay_executor<execution::require_member_result_t<InnerExecutor&&, Property>>
        { return { delay_, std::move(inner_ex_).require(p), std::is_same<Property, execution::never_blocking_t>::value ? true : is_never_blocking_ }; }

    // Intercept query requests for the delay.
    std::chrono::steady_clock::duration query(custom_properties::nonblocking_oneway_delay) const { return delay_; }

    // Forward other kinds of query to the inner executor.
    template<class Property> auto query(const Property& p) const
      -> typename execution::query_member_result<InnerExecutor, Property>::type
        { return inner_ex_.query(p); }

    friend bool operator==(const nonblocking_oneway_delay_executor& a, const nonblocking_oneway_delay_executor& b) noexcept
    {
      return a.delay_ == b.delay_
        && a.is_never_blocking_ == b.is_never_blocking_
        && a.inner_ex_ == b.inner_ex_;
    }

    friend bool operator!=(const nonblocking_oneway_delay_executor& a, const nonblocking_oneway_delay_executor& b) noexcept
    {
      return !(a == b);
    }

    template <class Function>
    auto execute(Function f) const
      -> decltype(inner_declval<Function>().execute(std::move(f)))
    {
      return inner_ex_.execute(
          [delay = delay_, f = std::move(f), is_never_blocking = is_never_blocking_]() mutable
          {
            if (is_never_blocking)
            {
              std::cout << "adding a delay\n";
              std::this_thread::sleep_for(delay);
            }
            return f();
          });
    }

    template <class Function>
    auto twoway_execute(Function f) const
      -> decltype(inner_declval<Function>().twoway_execute(std::move(f)))
    {
      // Two-way functions don't have a delay.
      return inner_ex_.twoway_execute(std::move(f));
    }
  };

  template <class Executor>
    std::enable_if_t<!execution::has_require_member_v<Executor, nonblocking_oneway_delay>, nonblocking_oneway_delay_executor<Executor>>
      require(Executor ex, nonblocking_oneway_delay d) { return { d.duration, std::move(ex) }; }

  // This hint cannot be preferred.
  template <class Executor>
    void prefer(Executor ex, nonblocking_oneway_delay d) = delete;
}

class inline_executor
{
public:
  friend bool operator==(const inline_executor&, const inline_executor&) noexcept
  {
    return true;
  }

  friend bool operator!=(const inline_executor&, const inline_executor&) noexcept
  {
    return false;
  }

  template <class Function>
  void execute(Function f) const noexcept
  {
    std::cout << "running function inline\n";
    f();
  }
};

static_assert(execution::is_oneway_executor_v<inline_executor>, "one way executor requirements not met");
static_assert(execution::is_oneway_executor_v<custom_properties::nonblocking_oneway_delay_executor<static_thread_pool::executor_type>>, "one way executor requirements not met");

int main()
{
  static_thread_pool pool{1};

  auto ex1 = execution::require(inline_executor(), custom_properties::nonblocking_oneway_delay{std::chrono::seconds(1)});
  assert(execution::query(ex1, custom_properties::nonblocking_oneway_delay{}) == std::chrono::seconds(1));
  ex1.execute([]{ std::cout << "we made it\n"; });

  static_assert(!execution::can_prefer_v<inline_executor, custom_properties::nonblocking_oneway_delay>, "cannot prefer");

  auto ex3 = execution::require(pool.executor(), execution::always_blocking);
  static_assert(!execution::can_prefer_v<static_thread_pool::executor_type, custom_properties::nonblocking_oneway_delay>, "cannot prefer");

  auto ex4 = execution::require(ex3, custom_properties::nonblocking_oneway_delay{std::chrono::seconds(1)});
  assert(execution::query(ex4, custom_properties::nonblocking_oneway_delay{}) == std::chrono::seconds(1));
  ex4.execute([]{ std::cout << "we made it again\n"; });

  auto ex5 = execution::require(pool.executor(), execution::never_blocking);
  auto ex6 = execution::require(ex5, custom_properties::nonblocking_oneway_delay{std::chrono::seconds(1)});
  assert(execution::query(ex6, custom_properties::nonblocking_oneway_delay{}) == std::chrono::seconds(1));
  ex6.execute([]{ std::cout << "we made it with a delay\n"; });

  auto ex7 = execution::require(ex4, execution::never_blocking);
  assert(execution::query(ex7, custom_properties::nonblocking_oneway_delay{}) == std::chrono::seconds(1));
  ex7.execute([]{ std::cout << "we made it again with a delay\n"; });

  pool.wait();
}
