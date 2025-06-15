/**
 * @file rtx_bsd_tcp.cpp
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-01
 */

#pragma once
#include "io/rtx.h"
#include "net_common.h"
#include <stdint.h>
#include <stdio.h>

namespace net {
struct rtx_bsd_tcp_cfg_t {
    net::common::addr_t addr;
    int fd = -1;
    //
    int16_t non_blocking = 1;
    int16_t timeout_ms = 3000;
};
//
class rtx_bsd_tcp : public detail::io::rtx<rtx_bsd_tcp, rtx_bsd_tcp_cfg_t> {
public:
    bool open();

    void close();

    int read(char *data, size_t len);

    int write(const void *data, size_t len);
};
}  // namespace net
