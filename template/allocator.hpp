/**
 * @file allocator.hpp
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2024-12-22
 */
#pragma once
#include <vector>


template <typename T>
class Allocator {

    T*      acquire(){
        return nullptr;
    }
    
    void    reclaim(T*  ptr) {
        
    }
private:
    std::vector<T>    cache_;
};
