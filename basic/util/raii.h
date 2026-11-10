/**
 * @file raii.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-08-09
 */
#pragma once
#include <functional>
#include <type_traits>

namespace detail {

template <typename T>
struct no_const {
    using type = typename std::conditional<std::is_const<T>::value, typename std::remove_const<T>::type, T>::type;
};

class RAII {
public:
    using func_type = std::function<void()>;

    explicit RAII(
        func_type release, func_type acquire = []() {}, bool commit = true) noexcept
        : commit_(commit), release_func_(release) {
        /* code */
    }
    /**
     *
     */
    ~RAII() noexcept {
        if (commit_) {
            release_func_();
        }
    }
    /**
     *
     */
    RAII(RAII &&other) noexcept {
        if (this != &other) {
            commit_ = std::move(other.commit_);
            other.commit_ = false;
            release_func_ = std::move(other.release_func_);
        }
    }

    RAII(const RAII &other) = delete;

    RAII &operator=(const RAII &other) = delete;

    RAII &commit(bool flag = true) {
        commit_ = flag;
        return *this;
    }

private:
    bool commit_{false};
    func_type release_func_;
};

/**]
 * RAII 实体类
 */

template <typename T>
class RAIIVar {
public:
    using _Self = RAIIVar<T>;
    using acq_type = std::function<T()>;
    using rel_type = std::function<void(T &)>;

public:
    explicit RAIIVar(acq_type acquire, rel_type release) : resource_(acquire()), rel_(release) {
    }

    ~RAIIVar() {
        if (commit_) {
            rel_(resource_);
        }
    }

    RAIIVar(RAIIVar &&other) noexcept {
        commit_ = other.commit_;
        resource_ = std::move(other.resource_);
        rel_ = std::move(other.rel_);
        other.commit_ = false;
    }

    _Self &commit(bool flag = true) noexcept {
        commit_ = flag;
        return *this;
    }

    T &get() noexcept {
        return resource_;
    }

    T &operator*() noexcept {
        return resource_;
    }

    template <typename _T = T>
    typename std::enable_if<std::is_pointer<_T>::value, _T>::type operator->() noexcept {
        return resource_;
    }

    template <typename _T = T>
    typename std::enable_if<std::is_class<_T>::value, _T *>::type operator->() noexcept {
        return std::addressof(resource_);
    }

private:
    bool commit_;
    T resource_;
    rel_type rel_;
};

template <typename RES, typename M_REL, typename M_ACQ>
RAII make_raii(RES &res, M_REL rel, M_ACQ acq, bool default_com = true) noexcept {
    // 编译时检查参数类型
    // 静态断言中用到的is_class,is_member_function_pointer等是用于编译期的计算、查询、判断、转换的type_traits类,
    // 有点类似于java的反射(reflect)提供的功能,不过只能用于编译期，不能用于运行时。
    // 关于type_traits的详细内容参见:http://www.cplusplus.com/reference/type_traits/
    static_assert(std::is_class<RES>::value, "RES is not a class or struct type.");
    static_assert(std::is_member_function_pointer<M_REL>::value, "M_REL is not a member function.");
    static_assert(std::is_member_function_pointer<M_ACQ>::value, "M_ACQ is not a member function.");
    assert(nullptr != rel && nullptr != acq);
    auto p_res = std::addressof(const_cast<typename no_const<RES>::type &>(res));
    return RAII(std::bind(rel, p_res), std::bind(acq, p_res), default_com);
}
/* 创建 raii 对象 无需M_ACQ的简化版本 */
template <typename RES, typename M_REL>
RAII make_raii(RES &res, M_REL rel, bool default_com = true) noexcept {
    static_assert(std::is_class<RES>::value, "RES is not a class or struct type.");
    static_assert(std::is_member_function_pointer<M_REL>::value, "M_REL is not a member function.");
    assert(nullptr != rel);
    auto p_res = std::addressof(const_cast<typename no_const<RES>::type &>(res));
    return RAII(std::bind(rel, p_res), [] {}, default_com);
}

}  // namespace detail
