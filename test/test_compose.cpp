
#include <type_traits>
#include <utility>

struct A {
    int a;
    double b;
};

struct B {
    char c;
    float d;
};
struct C {
    long e;
    short f;
};

template <typename... Ts>
struct Compose : Ts... {
    using Ts::Ts...;  // Inherit constructors

    A *getA() {
        return dynamic_cast<A *>(this);
    }
    B *getB() {
        return dynamic_cast<B *>(this);
    }
    C *getC() {
        return dynamic_cast<C *>(this);
    }

    template <typename T>
    T *get() {
        return dynamic_cast<T *>(this);
    }
};

#include <iostream>
#include <type_traits>

// 简单的类型包装器
template <typename T>
struct TypeWrapper {
    using type = T;
    static constexpr const char *name = "unknown";
};

// 特化类型信息
template <>
struct TypeWrapper<int> {
    using type = int;
    static constexpr const char *name = "int";
};
template <>
struct TypeWrapper<double> {
    using type = double;
    static constexpr const char *name = "double";
};

// 类型组合模板
template <typename... Types>
struct TypeList {};

// 使用示例
using MyTypes = TypeList<int, double, char, 123>;

#include <tuple>
#include <typeindex>
#include <unordered_map>

// 组件基类
struct Component {
    virtual ~Component() = default;
};

// 具体组件
struct Position {
    double x, y, z;
};

struct Velocity {
    double dx, dy, dz;
};

struct Health  {
    int current, max;
};

// 实体作为组件组合
template <typename... Components>
struct Entity {
    std::tuple<Components...> components;

public:
    template <typename T>
    T &get() {
        return std::get<T>(components);
    }

    template <typename T>
    const T &get() const {
        return std::get<T>(components);
    }

    template <typename T, typename... Args>
    void set(Args &&...args) {
        std::get<T>(components) = T{std::forward<Args>(args)...};
    }

    static constexpr size_t component_count = sizeof...(Components);
};

// 使用示例
using Player = Entity<Position, Velocity, Health>;
using StaticObject = Entity<Position>;

int main() {
    // 测试属性组合系统
    // PersonAttributes person;
    // person.set<name_attr>("John");
    // person.set<age_attr>(25);
    // person.set<score_attr>(95.5);

    // std::cout << "Name: " << person.get<name_attr>() << std::endl;
    // std::cout << "Age: " << person.get<age_attr>() << std::endl;
    // std::cout << "Score: " << person.get<score_attr>() << std::endl;

    // 测试实体组合
    Player player;
    player.set<Position>(1.0, 2.0, 3.0);
    player.set<Velocity>(0.1, 0.2, 0.0);
    player.set<Health>(100, 100);

    std::cout << sizeof(Player) << std::endl;
    std::cout << sizeof(Player::component_count) << std::endl;
    std::cout << sizeof(Player::components) << std::endl;
    std::cout << sizeof(Position) << std::endl;
    std::cout << sizeof(Velocity) << std::endl;
    std::cout << sizeof(Health) << std::endl;
    std::cout << "Player Position: (" << player.get<Position>().x << ", " << player.get<Position>().y << ", "
              << player.get<Position>().z << ")" << std::endl;
    std::cout << "Player Velocity: (" << player.get<Velocity>().dx << ", " << player.get<Velocity>().dy << ", "
              << player.get<Velocity>().dz << ")" << std::endl;
    std::cout << "Player Health: " << player.get<Health>().current << "/" << player.get<Health>().max << std::endl;

    return 0;
}

// int main () {
//     printf("%d\n", sizeof(Compose<A, B, C>));

//     printf("%d\n", sizeof(Compose<A, B>));

//     Compose<A, B> obj;
//     auto aa = obj.get<A>();
//     aa->a = 20;
//     aa->b = 3.14;

//     auto bb = obj.get<B>();
//     bb->c = 'x';
//     bb->d = 2.71f;

//     auto cc = obj.get<C>();
//     if (cc == nullptr) {
//         printf("C is nullptr\n");
//     }

//     printf("A::a:%d, A::b:%f\n", aa->a, aa->b);
//     printf("B::c:%c, B::d:%f\n", bb->c, bb->d);
//     return 0;
// }