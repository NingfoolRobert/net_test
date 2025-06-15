#include "rx_bsd_mcbc.h"
#include "net_util.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

bool rx_bsd_mcbc::open() {

    memset(&sockaddr_, 0, sizeof(sockaddr_));
    fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd_ < 0) {
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
    if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof(tv)) < 0) {
        perror("set socket recv timeout fail.");
        return false;
    }
    //
    sockaddr_ = opt_.pair.dst.to_sockaddr();
    if (::bind(fd_, (struct sockaddr *)&sockaddr_, sizeof(sockaddr_)) < 0) {
        perror("bind fail.");
        return false;
    }
    //
    if (!opt_.non_blocking) {
        //
        int flag = fcntl(fd_, F_GETFL, 0);
        flag |= O_NONBLOCK;
        if (fcntl(fd_, F_SETFL, flag) == -1) {
            perror("set non blocking fail.");
            return false;
        }
    }
    //
    if (opt_.join_multicast && net::util::is_multicast_addr(opt_.pair.dst.ip)) {
        struct ip_mreq mc_addr;
        mc_addr.imr_multiaddr.s_addr = inet_addr(opt_.pair.dst.ip);
        mc_addr.imr_interface.s_addr =
            inet_addr(net::util::if_name_to_ipv4(net::util::if_index_to_name(opt_.pair.if_idx).c_str()).c_str());
        if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mc_addr, sizeof(mc_addr)) < 0) {
            perror("add multicast group fail");
            return false;
        }
    }

    return true;
}

void rx_bsd_mcbc::close() {
    if (fd_ < 0) {
        return;
    }
    //
    if (opt_.join_multicast) {
        // 
        struct ip_mreq mc_addr;
        mc_addr.imr_multiaddr.s_addr = inet_addr(opt_.pair.dst.ip);
        mc_addr.imr_interface.s_addr =
            inet_addr(net::util::if_name_to_ipv4(net::util::if_index_to_name(opt_.pair.if_idx).c_str()).c_str());
        if (::setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mc_addr, sizeof(mc_addr)) < 0) {
            perror("drop multicast group fail");
        }
      
    }
    //
    ::close(fd_);
}

int rx_bsd_mcbc::read(char *data, int len) {
    struct sockaddr_in cli_sockaddr;
    socklen_t s_len = sizeof(cli_sockaddr);
    //
    return ::recvfrom(fd_, data, len, 0, (struct sockaddr *)&cli_sockaddr, &s_len);
}
}  // namespace net
