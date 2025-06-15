#include "rtx_bsd_tcp.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>

namespace net {

bool rtx_bsd_tcp::open() {

    if (opt_.fd < 0) {
        opt_.fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (opt_.fd < 0) {
            perror("open tcp socket fail.");
            return false;
        }
    }
    //
    auto sockaddr = opt_.addr.to_sockaddr();
    if (::connect(opt_.fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
        perror("connect server fail");
        return false;
    }
    //
    if (opt_.non_blocking) {
        int flag = fcntl(opt_.fd, F_GETFL);
        flag |= O_NONBLOCK;
        if (::fcntl(opt_.fd, F_SETFL, flag) < 0) {
            perror("set non blocking fail");
            return false;
        }
    }
    else {
        struct timeval tv = {.tv_sec = opt_.timeout_ms / 1000, .tv_usec = opt_.timeout_ms % 1000 * 1000};
        if (::setsockopt(opt_.fd, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof(tv)) < 0) {
            perror("set recv timeout time fail");
            return false;
        }
        //
        if (::setsockopt(opt_.fd, SOL_SOCKET, SO_SNDTIMEO, (const void *)&tv, sizeof(tv)) < 0) {
            perror("set send timeout time fail");
            return false;
        }
    }
    //
    return true;
}

void rtx_bsd_tcp::close() {
    if (opt_.fd < 0) {
        return;
    }
    ::close(opt_.fd);
    opt_.fd = -1;
}

int rtx_bsd_tcp::read(char *data, size_t len) {
    return ::recv(opt_.fd, data, len 0);
}

int rtx_bsd_tcp::write(const void *data, size_t len) {
    return ::send(opt_.fd, data, len, 0);
}
}  // namespace net
