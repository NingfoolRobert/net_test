/**
 * @file tx_bsd_mcbc.cpp
 * @brief
 * @author
 * @version 0.1
 * @date 2025-03-01
 */
#pragma once
#include "io/tx.h"
#include "net_types.h"
#include "net_util.h"
#include <stdint.h>

namespace net {
struct tx_bsd_mcbc_cfg_t {
    net::ip_pair_t pair;
    int8_t join_multicast = 0;
    int8_t non_blocking = 1;
    int16_t loopback = 0;
    int16_t ip_pkt_ttl = 64;
    uint16_t timeout_ms = 3000;
};
//
class tx_bsd_mcbc : public detail::io::tx<tx_bsd_mcbc, tx_bsd_mcbc_cfg_t> {
public:
    tx_bsd_mcbc() = default;
    ~tx_bsd_mcbc() { close(); }
    tx_bsd_mcbc(const tx_bsd_mcbc &) = delete;
    tx_bsd_mcbc &operator=(const tx_bsd_mcbc &) = delete;

private:
    friend class detail::io::io<tx_bsd_mcbc, tx_bsd_mcbc_cfg_t>;
    friend class detail::io::tx<tx_bsd_mcbc, tx_bsd_mcbc_cfg_t>;

    bool open_impl();

    void close_impl();

    int send_impl(const void *data, size_t len);

private:
    int fd_{-1};
    struct sockaddr_in sockaddr_{};
};
}  // namespace net
