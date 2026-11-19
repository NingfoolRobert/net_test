// 配置基类
#include <iostream>
#include <string>
#include <array>
#include <type_traits>


template <typename Derived>
struct BaseConfig {
    using DerivedType = Derived;
    
    virtual void validate() const = 0;
    virtual ~BaseConfig() = default;
};

// CRTP 基类
template <typename Derived, typename Config>
class ConfiguredBase {
    static_assert(
       std::is_base_of_v<BaseConfig<Derived>, Config>,
        "Config must inherit from BaseConfig<Derived>"
    );
    
protected:
    Config config_;
    
public:
    // 类型推导辅助
    using DerivedType = Derived;
    using ConfigType = Config;
    
    // 获取配置（const 和非 const 版本）
    const Config& getConfig() const & {
        config_.validate();
        return config_;
    }
    
    Config& getConfig() & {
        config_.validate();
        return config_;
    }
    
    // 配置设置
    void configure(Config cfg) {
        cfg.validate();
        config_ = std::move(cfg);
    }
    
    // CRTP 模式的核心：派生到基类的转换
    Derived& derived() {
        return static_cast<Derived&>(*this);
    }
    
    const Derived& derived() const {
        return static_cast<const Derived&>(*this);
    }
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////
// 具体配置类
struct DatabaseConfig : BaseConfig<DatabaseConfig> {
    std::string connectionString;
    int timeoutMs = 5000;
    int maxConnections = 10;
    
    void validate() const override {
        if (connectionString.empty()) {
            throw std::runtime_error("Connection string is empty");
        }
        if (timeoutMs <= 0) {
            throw std::runtime_error("Timeout must be positive");
        }
    }
    
    // 类型安全的方法链
    DatabaseConfig& withConnectionString(std::string conn) {
        connectionString = std::move(conn);
        return *this;
    }
    
    DatabaseConfig& withTimeout(int ms) {
        timeoutMs = ms;
        return *this;
    }
};
// 具体派生类
class DatabaseClient : public ConfiguredBase<DatabaseClient, DatabaseConfig> {
public:
    void connect() {
        // 使用配置
        std::cout << "Connecting to: " << config_.connectionString 
                  << " with timeout: " << config_.timeoutMs << "ms\n";
    }
    
    void executeQuery(const std::string& query) {
        // 派生类特定的实现
        std::cout << "Executing query: " << query << std::endl;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 配置策略
struct FastPolicy {
    static constexpr int cacheSize = 1024;
    static constexpr bool useCompression = false;
};

struct MemoryEfficientPolicy {
    static constexpr int cacheSize = 256;
    static constexpr bool useCompression = true;
};

// 配置感知的 CRTP
template <typename Derived, typename Policy>
class PolicyBasedBase {
public:
    static constexpr auto cacheSize = Policy::cacheSize;
    static constexpr auto useCompression = Policy::useCompression;
    
    void applyPolicy() {
        std::cout << "Cache size: " << cacheSize 
                  << ", Compression: " << (useCompression ? "Yes" : "No")
                  << std::endl;
    }
};

// 使用策略
class CustomService : public PolicyBasedBase<CustomService, FastPolicy> {
public:
    void run() {
        applyPolicy();
        // 可以使用策略中的常量
        std::array<int, cacheSize> cache;
        // ...
    }
};