/**
 * @file rx_bsd_mcbc.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-01
 */
#pragma once

#include "common/net_types.h"
#include "io/rx.h"
#include <stdint.h>
#include <sys/socket.h>

namespace net {

struct rx_bsd_mcbc_cfg_t {
    net::ip_pair_t pair;
    int8_t join_multicast = 0;
    int8_t non_blocking = 1;
    uint16_t timeout_ms = 3000;
};

class rx_bsd_mcbc : public detail::io::rx<rx_bsd_mcbc, rx_bsd_mcbc_cfg_t> {
public:
    rx_bsd_mcbc() = default;
    ~rx_bsd_mcbc() { close(); }
    rx_bsd_mcbc(const rx_bsd_mcbc &) = delete;
    rx_bsd_mcbc &operator=(const rx_bsd_mcbc &) = delete;

private:
    friend class detail::io::io<rx_bsd_mcbc, rx_bsd_mcbc_cfg_t>;
    friend class detail::io::rx<rx_bsd_mcbc, rx_bsd_mcbc_cfg_t>;

    bool open_impl();

    void close_impl();

    int recv_impl(char *data, size_t len);

private:
    int fd_{-1};
    struct sockaddr_in sockaddr_;
};
}  // namespace net
