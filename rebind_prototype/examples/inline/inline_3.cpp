#include <experimental/execution>
#include <iostream>

namespace execution = std::experimental::execution;

class inline_executor
{
public:
  auto& context() const noexcept { return *this; }

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
    f();
  }
};

static_assert(execution::is_one_way_executor_v<inline_executor>, "one way executor requirements not met");
static_assert(execution::is_two_way_executor_v<decltype(execution::rebind(inline_executor(), execution::two_way))>, "two way executor requirements not met");

int main()
{
  inline_executor ex1;
  auto ex2 = execution::rebind(ex1, execution::two_way);
  std::future<int> f = ex2.async_execute([]{ return 42; });
  std::cout << "result is " << f.get() << "\n";
}
