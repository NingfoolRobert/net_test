#include "../../../network/io/bsd/rtx_bsd_tcp.h"
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>

TEST(BasicTcpIo, SendAndRecvThroughBaseUseConfiguredSocket) {
    int pair[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    struct Peer {
        int fd;
        ~Peer() { ::close(fd); }
    } peer{pair[1]};
    net::rtx_bsd_tcp tcp;
    detail::io::rtx<net::rtx_bsd_tcp, net::rtx_bsd_tcp_cfg_t> &base = tcp;
    net::rtx_bsd_tcp_cfg_t cfg{};
    cfg.fd = pair[0];
    base.set_opt(cfg);
    timeval timeout{1, 0};
    ASSERT_EQ(::setsockopt(pair[0], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);
    ASSERT_EQ(::setsockopt(pair[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);
    ASSERT_EQ(base.send("hello", 5), 5);
    char data[5];
    ASSERT_EQ(::recv(peer.fd, data, sizeof(data), MSG_WAITALL), 5);
    EXPECT_EQ(std::string(data, 5), "hello");
    ASSERT_EQ(::send(peer.fd, "world", 5, 0), 5);
    ASSERT_EQ(base.recv(data, sizeof(data)), 5);
    EXPECT_EQ(std::string(data, 5), "world");
    base.close();
    EXPECT_EQ(base.get_opt().fd, -1);
}

TEST(BasicTcpIo, ErrorsAreCapturedFromDirectDerivedCalls) {
    net::rtx_bsd_tcp tcp;
    char data[4];
    EXPECT_EQ(tcp.recv(data, sizeof(data)), -1);
    EXPECT_EQ(tcp.last_error(), EBADF);
    EXPECT_EQ(tcp.error_code().value(), EBADF);
    EXPECT_EQ(tcp.send("test", 4), -1);
    EXPECT_EQ(tcp.last_error(), EBADF);
    tcp.close();
    EXPECT_EQ(tcp.last_error(), 0);
}
