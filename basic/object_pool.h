/**
 * @file object_pool.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-06-13
 */
#pragma once
#ifndef _OBJECT_POOL_H_
#define _OBJECT_POOL_H_

namespace basic {
template <typename T>
class ObjectPool {
public:
    T *acquire() {
        return nullptr;
    }
    
    void release(T*& p) {
        //
        
        p = nullptr;
    }

private:
    
};
}  // namespace basic

#endif
