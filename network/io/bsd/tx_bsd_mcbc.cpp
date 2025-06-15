#include "tx_bsd_mcbc.h"
#include "net_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

namespace net {

bool tx_bsd_mcbc::open() {

    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd_ < 0) {
        perror("create socket fail");
        return false;
    }
    //
    int opt = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt)) != 0) {
        perror("set sock opt fail");
        return false;
    }
    //
    struct timeval tv = {.tv_sec = opt_.timeout_ms / 1000, .tv_usec = opt_.timeout_ms % 1000 * 1000};
    if (::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, (const void *)&tv, sizeof(tv)) < 0) {
        perror("set send timeout fail.");
        return false;
    }
    //
    if (opt_.join_multicast && net::util::is_multicast_addr(opt_.pair.dst.ip)) {
        // 
        if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, (const void *)&opt_.ip_pkt_ttl, sizeof(opt_.ip_pkt_ttl)) <
            0) {
            perror("set multicast ttl fail");
            return false;
        }
        // 
        if(::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, (const void*)&opt_.loopback, sizeof(opt_.loopback)) < 0) {
          perror("set multicast loopback fail.");
          return false;
        }
    }
    //
    sockaddr_ = this->opt_.pair.dst.to_sockaddr();
    if (::connect(fd_, (struct sockaddr *)&sockaddr_, sizeof(sockaddr_)) == -1) {
        return false;
    }
    //
    return true;
}

void tx_bsd_mcbc::close() {

    if (fd_ < 0) {
        return;
    }
    //
    ::close(fd_);
    fd_ = -1;
}

int tx_bsd_mcbc::write(const void *data, size_t len) {
    return ::sendto(fd_, data, len, 0, (struct sockaddr *)&sockaddr_, sizeof(sockaddr_));
}
}  // namespace net
