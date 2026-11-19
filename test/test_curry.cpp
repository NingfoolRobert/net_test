
#include <iostream>
#include <functional>
#include <utility>
#include <string>
#include <tuple>
// Add index_sequence support for C++11 compatibility if needed
#if __cplusplus < 201402L
namespace std {
    template<std::size_t... Ints>
    struct index_sequence {
        using type = index_sequence;
        using value_type = std::size_t;
        static constexpr std::size_t size() noexcept { return sizeof...(Ints); }
    };

    template<class Sequence1, class Sequence2>
    struct merge_index_sequence;

    template<std::size_t... I1, std::size_t... I2>
    struct merge_index_sequence<index_sequence<I1...>, index_sequence<I2...>>
        : index_sequence<I1..., (sizeof...(I1) + I2)...> {};

    template<std::size_t N>
    struct make_index_sequence {
        using type = typename merge_index_sequence<typename make_index_sequence<N / 2>::type,
                                                  typename make_index_sequence<N - N / 2>::type>::type;
    };

    template<>
    struct make_index_sequence<0> : index_sequence<> {};

    template<>
    struct make_index_sequence<1> : index_sequence<0> {};
}
#endif
// 第一步：函数类型萃取辅助模板（提取std::function的返回值和参数类型）
template <typename T>
struct function_traits;

// 特化：针对std::function的成员函数调用运算符
template <typename R, typename... Args>
struct function_traits<std::function<R(Args...)>> {
    using return_type = R;                    // 函数返回值类型
    static constexpr std::size_t arity = sizeof...(Args); // 函数参数个数
};

// 第二步：普通函数/函数指针转换为std::function的辅助函数
template <typename R, typename... Args>
std::function<R(Args...)> to_std_function(R(*func)(Args...)) {
    return std::function<R(Args...)>(func);
}

// 辅助函数：使用 tuple 展开调用函数
template <typename Func, typename Tuple, std::size_t... I>
auto apply_tuple(Func&& func, Tuple&& t, std::index_sequence<I...>) {
    return func(std::get<I>(std::forward<Tuple>(t))...);
}

// 第三步：核心Curry化模板（递归版本+终止版本）
template <typename Func, typename... BoundArgs>
auto curry(Func&& func, BoundArgs&&... bound_args) {
    // 萃取函数的参数个数和返回值类型
    using func_type = std::decay_t<Func>;
    static constexpr std::size_t total_args = function_traits<func_type>::arity;
    static constexpr std::size_t current_args = sizeof...(BoundArgs);

    // 编译期判断：是否已绑定所有参数（终止条件 vs 递归条件）
    if constexpr (current_args == total_args) {
        // 终止版本：所有参数已绑定，执行原函数并返回结果
        return func(std::forward<BoundArgs>(bound_args)...);
    }
    else {
        // 递归版本：返回一个lambda，接收下一个单个参数，继续Curry化
        // 使用 tuple 存储已绑定的参数
        auto bound_tuple = std::make_tuple(std::forward<BoundArgs>(bound_args)...);
        return[
            func = std::forward<Func>(func),
                bound_tuple = std::move(bound_tuple)
        ] (auto&& next_arg) mutable {
                // 展开 tuple 并添加新参数
                return std::apply([&](auto&&... args) {
                    return curry(
                        func,
                        std::forward<decltype(args)>(args)...,
                        std::forward<decltype(next_arg)>(next_arg)
                    );
                    }, bound_tuple);
            };
    }
}

// 第四步：Curry化入口封装（支持直接传入普通函数，无需手动转换std::function）
template <typename R, typename... Args>
auto make_curry(R(*func)(Args...)) {
    auto std_func = to_std_function(func);
    return curry(std_func); // 初始调用：无已绑定参数，开始Curry化
}

// 测试用例1：多参数数值计算函数（3个参数求和）
int three_add(int a, int b, int c) {
    return a + b + c;
}

// 测试用例2：多参数字符串处理函数（4个参数拼接）
std::string four_concat(std::string a, std::string b, std::string c, std::string d) {
    return a + " -> " + b + " -> " + c + " -> " + d;
}

// 测试用例3：2个参数的乘法函数
double two_multiply(double a, double b) {
    return a * b;
}

int main() {
    // 测试1：3参数求和函数的Curry化
    auto curried_add = make_curry(three_add);
    // 分步传入参数
    auto add_5 = curried_add(5);        // 绑定第一个参数5
    auto add_5_10 = add_5(10);          // 绑定第二个参数10
    int add_result = add_5_10(15);      // 绑定第三个参数15，执行函数（终止）

    // 链式调用简化写法
    int add_chain_result = make_curry(three_add)(100)(200)(300);

    // 测试2：4参数字符串拼接函数的Curry化
    std::string concat_result = make_curry(four_concat)("C++")("Curry")("Template")("Programming");

    // 测试3：2参数乘法函数的Curry化
    double multiply_result = make_curry(two_multiply)(3.14)(2.0);

    // 输出所有结果
    std::cout << "three_add(5, 10, 15) = " << add_result << std::endl;
    std::cout << "three_add(100, 200, 300) = " << add_chain_result << std::endl;
    std::cout << "four_concat(C++, Curry, Template, Programming) = " << concat_result << std::endl;
    std::cout << "two_multiply(3.14, 2.0) = " << multiply_result << std::endl;

    return 0;
}