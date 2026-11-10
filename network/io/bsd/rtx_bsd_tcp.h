/**
 * @file rtx_bsd_tcp.cpp
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-01
 */

#pragma once
#include "io/rtx.h"
#include "net_types.h"
#include <stdint.h>
#include <stdio.h>

namespace net {
struct rtx_bsd_tcp_cfg_t {
    net::addr_t addr;
    int fd = -1;
    //
    int16_t non_blocking = 1;
    int16_t timeout_ms = 3000;
};
//
class rtx_bsd_tcp : public detail::io::rtx<rtx_bsd_tcp, rtx_bsd_tcp_cfg_t> {
public:
    rtx_bsd_tcp() = default;
    ~rtx_bsd_tcp() { close(); }
    rtx_bsd_tcp(const rtx_bsd_tcp &) = delete;
    rtx_bsd_tcp &operator=(const rtx_bsd_tcp &) = delete;

private:
    friend class detail::io::io<rtx_bsd_tcp, rtx_bsd_tcp_cfg_t>;
    friend class detail::io::rtx<rtx_bsd_tcp, rtx_bsd_tcp_cfg_t>;

    bool open_impl();

    void close_impl();

    int recv_impl(char *data, size_t len);

    int send_impl(const void *data, size_t len);
};
}  // namespace net
