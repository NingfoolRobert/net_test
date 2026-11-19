
/**
 * @brief
 *
 */
#pragma once

#include "common/net_types.h"
#include <utility>
namespace net {
class socket_t {
  public:
  socket_t() = default;
  socket_t(const socket_t &)            = delete;
  socket_t &operator=(const socket_t &) = delete;

  socket_t(socket_t &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {
  }

  socket_t &operator=(socket_t &&other) noexcept {
    if (this != &other) {
      close();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  ~socket_t() {
    close();
  }

  int open(int domain, int type, int protocol) {
    fd_ = ::socket(domain, type, protocol);
    return fd_;
  }

  int bind(addr_t *addr) const {
    struct sockaddr_in in_addr{};
    in_addr.sin_addr.s_addr = inet_addr(addr->ip);
    in_addr.sin_port        = htons(addr->port);
    in_addr.sin_family      = AF_INET;

    return ::bind(fd_, (struct sockaddr *)(&in_addr), sizeof(in_addr));
  }

  int connect(addr_t &addr) const {
    struct sockaddr_in in_addr{};
    in_addr.sin_addr.s_addr = inet_addr(addr.ip);
    in_addr.sin_port        = htons(addr.port);
    in_addr.sin_family      = AF_INET;

    return ::connect(fd_, reinterpret_cast<struct sockaddr *>(&in_addr), sizeof(in_addr));
  }

  int listen(int backlog) const {
    return ::listen(fd_, backlog);
  }

  socket_t accept(addr_t *addr) const {
    struct sockaddr_in in_addr{};
    socklen_t          addrlen = sizeof(in_addr);
    int                new_fd  = ::accept(fd_, reinterpret_cast<struct sockaddr *>(&in_addr), &addrlen);
    if (new_fd < 0) {
      return socket_t();
    }

    strcpy(addr->ip, inet_ntoa(in_addr.sin_addr));
    addr->port = ntohs(in_addr.sin_port);

    socket_t new_socket;
    new_socket.set_fd(new_fd);
    return new_socket;
  }

  int send(const char *buf, size_t size, int flag = 0) const {
    return ::send(fd_, buf, size, flag);
  }

  int sendto(const char *buf, size_t size, const struct sockaddr *to, socklen_t to_len, int flag = 0) const {
    return ::sendto(fd_, buf, size, flag, to, to_len);
  }

  int recv(char *buf, uint32_t size, int flag = 0) const {
    return ::recv(fd_, buf, size, flag);
  }

  int setsockopt(int level, int optname, const void *optval, size_t optlen) const {
    return ::setsockopt(fd_, level, optname, optval, optlen);
  }

  int getsockopt(int level, int optname, void *optval, size_t *optlen) const {
    socklen_t len = *optlen;
    return ::getsockopt(fd_, level, optname, optval, &len);
  }

  void close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  int ioctl(int request, void *argp) const {
    return ::ioctl(fd_, request, argp);
  }

  void set_fd(int fd) {
    fd_ = fd;
  }

  bool is_invalid_socket() const {
    return fd_ < 0;
  }

  public:
  int fd_ = -1;
};
}  // namespace net
