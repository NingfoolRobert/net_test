/**
 * @file rx_bsd_mcbc.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-01
 */
#pragma once

#include "common/net_common.h"
#include "io/rx.h"
#include <stdint.h>
#include <sys/socket.h>

namespace net {

struct rx_bsd_mcbc_cfg_t {
    net::common::ip_pair pair;
    int8_t join_multicast = 0;
    int8_t non_blocking = 1;
    uint16_t timeout_ms = 3000;
};

class rx_bsd_mcbc : public detail::io::rx<rx_bsd_mcbc, rx_bsd_mcbc_cfg_t> {
public:
    bool open();

    void close();

    int read(char *data, int len);

private:
    int fd_{-1};
    struct sockaddr_in sockaddr_;
};
}  // namespace net
