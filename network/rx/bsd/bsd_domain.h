/**
 * @file bsd_domain.h
 * @brief Unix domain stream socket client.
 */
#pragma once

#include "net_types.h"
#include <cstring>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace net {
struct bsd_domain_cfg_t {
  char sock_path[64]   = {0};
  bool is_busy_waiting = false;
};

class bsd_domain_listener_t;

class bsd_domain_t {
  public:
  explicit bsd_domain_t(const bsd_domain_cfg_t &cfg) : cfg_(cfg) {
  }

  bsd_domain_t(const bsd_domain_t &)            = delete;
  bsd_domain_t &operator=(const bsd_domain_t &) = delete;

  ~bsd_domain_t() {
    close();
  }

  bool connect() {
    if (fd_ >= 0) {
      return false;
    }

    const size_t path_len = ::strnlen(cfg_.sock_path, sizeof(cfg_.sock_path));
    if (path_len == 0 || path_len >= sizeof(sockaddr_un::sun_path)) {
      return false;
    }

    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
      return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, cfg_.sock_path, path_len + 1);
    if (::connect(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
      close();
      return false;
    }

    if (cfg_.is_busy_waiting) {
      int enabled = 1;
      if (::ioctl(fd_, FIONBIO, &enabled) < 0) {
        close();
        return false;
      }
    }

    return true;
  }

  void close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
    remove_owned_path();
  }

  int recv(char *data, int len) {
    return ::recv(fd_, data, len, 0);
  }

  int send(const char *data, int len) {
    return ::send(fd_, data, len, 0);
  }

  bool bind() {
    if (fd_ >= 0) {
      return false;
    }

    const size_t path_len = ::strnlen(cfg_.sock_path, sizeof(cfg_.sock_path));
    if (path_len == 0 || path_len >= sizeof(sockaddr_un::sun_path)) {
      return false;
    }

    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
      return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, cfg_.sock_path, path_len + 1);

    if (::bind(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
      close();
      return false;
    }
    owns_path_ = true;
    if (!record_path_identity()) {
      ::unlink(cfg_.sock_path);
      owns_path_ = false;
      close();
      return false;
    }

    if (::listen(fd_, 5) < 0) {
      close();
      return false;
    }

    if (cfg_.is_busy_waiting) {
      int enabled = 1;
      if (::ioctl(fd_, FIONBIO, &enabled) < 0) {
        close();
        return false;
      }
    }
    return true;
  }

  bsd_domain_t *accept() {
    if (fd_ < 0) {
      return nullptr;
    }

    const int client_fd = ::accept(fd_, nullptr, nullptr);
    if (client_fd < 0) {
      return nullptr;
    }
    if (cfg_.is_busy_waiting) {
      int enabled = 1;
      if (::ioctl(client_fd, FIONBIO, &enabled) < 0) {
        ::close(client_fd);
        return nullptr;
      }
    }
    return new bsd_domain_t(client_fd, cfg_);
  }

  private:
  friend class bsd_domain_listener_t;

  bool record_path_identity() {
    struct stat path_stat{};
    if (::lstat(cfg_.sock_path, &path_stat) < 0) {
      return false;
    }
    path_device_          = path_stat.st_dev;
    path_inode_           = path_stat.st_ino;
    path_identity_valid_  = true;
    return true;
  }

  void remove_owned_path() {
    if (!owns_path_) {
      return;
    }
    struct stat path_stat{};
    if (path_identity_valid_ && ::lstat(cfg_.sock_path, &path_stat) == 0 && path_stat.st_dev == path_device_ &&
        path_stat.st_ino == path_inode_) {
      ::unlink(cfg_.sock_path);
    }
    owns_path_          = false;
    path_identity_valid_ = false;
  }

  bsd_domain_t(int fd, const bsd_domain_cfg_t &cfg) : fd_(fd), cfg_(cfg) {
  }

  int              fd_{-1};
  bsd_domain_cfg_t cfg_;
  bool              owns_path_ = false;
  bool              path_identity_valid_ = false;
  dev_t             path_device_{};
  ino_t             path_inode_{};
};
}  // namespace net
