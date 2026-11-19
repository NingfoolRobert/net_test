/**
 * @file bind_processor_helper.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-22
 */
#ifndef _BIND_PROCESSOR_HELPER_
#define _BIND_PROCESSOR_HELPER_

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum CPUPriorityEnum : uint8_t
{
    LOWEST = 1,
    LOWER = 30,
    NOLMAR = 50,
    HIGHER = 70,
    HIGHEST = 99,
};

enum CPUPolicyEnum : uint8_t
{
    FIFO,
    RR,

    OTHER = 10,
};


static inline bool bind_processor(int cpu_id, CPUPolicyEnum policy, CPUPriorityEnum priority) {
    auto total = sysconf(_SC_NPROCESSORS_ONLN);
    if (cfg.cpu_id >= total) {
        perror("the cpu id over the processors.");
        return false;
    }
    //
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cfg.cpu_id, &mask);
    pid_t pid = getpid();
    if (sched_setaffinity(pid, sizeof(cpu_set_t), &mask) == -1) {
        perror("set cpu affinity fail.");
        return false;
    }
    return true;
}

//
static inline bool bind_processor_np(pthread_t tid,
                                     int cpu_id,
                                     CPUPolicyEnum policy = CPUPolicyEnum::RR,
                                     CPUPriorityEnum priority = CPUPriorityEnum::NOLMAR) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_id, &mask);
    if (pthread_setaffinity_np(tid, sizeof(cpu_set_t), &mask) == -1) {
        perror("set cpu affinity fail.");
        return false;
    }
    //
    struct sched_param param;
    param.sched_priority = priority;
    int sched_policy = SCHED_OTHER;
    switch (policy) {
        case FIFO:
            sched_policy = SCHED_FIFO;
            break;
        case RR:
            sched_policy = SCHED_RR;
            break;
        default:
            break;
    }
    //
    if (pthread_setschedparam(tid, sched_policy, &param) != 0) {
        perror("set sched param fail.");
        return false;
    }
    //
    return true;
}

//
static inline bool bind_processor_self(int cpu_id,
                                       CPUPolicyEnum policy = CPUPolicyEnum::RR,
                                       CPUPriorityEnum priority = CPUPriorityEnum::NOLMAR) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_id, &mask);
    auto tid = pthread_self();
    if (pthread_setaffinity_np(tid, sizeof(cpu_set_t), &mask) == -1) {
        perror("set cpu affinity fail");
        return false;
    }
    //
    struct sched_param param;
    param.sched_priority = priority;
    int sched_policy = SCHED_OTHER;
    switch (policy) {
        case FIFO:
            sched_policy = SCHED_FIFO;
            break;
        case RR:
            sched_policy = SCHED_RR;
            break;
        default:
            break;
    }
    //
    if (pthread_setschedparam(tid, sched_policy, &param) != 0) {
        perror("set sched param fail.");
        return false;
    }

    return true;
}
#endif
