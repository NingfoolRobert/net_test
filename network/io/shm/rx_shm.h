/**
 * @file rx_shm.h
 * @brief
 * @author NingfoolRobert
 */
#pragma once
#include <memory>

#include "../../../basic/ipc/fixed_shm.h"
#include "io/rx.h"

namespace net {
struct rx_shm_cfg_t {
  uint64_t size = 0;
  char file_path[256] = {0};
  char key[256] = {0};
  uint32_t id = 0;
};

class rx_shm : public detail::io::rx<rx_shm, rx_shm_cfg_t> {
 public:
  rx_shm() = default;
  explicit rx_shm(const rx_shm_cfg_t& cfg) { set_opt(cfg); }

  ~rx_shm() { close(); }
  rx_shm(const rx_shm&) = delete;
  rx_shm& operator=(const rx_shm&) = delete;

  void set_cfg(const rx_shm_cfg_t& cfg) { set_opt(cfg); }

  rx_shm_cfg_t& get_cfg() { return get_opt(); }

  const rx_shm_cfg_t& get_cfg() const { return get_opt(); }

 private:
  friend class detail::io::io<rx_shm, rx_shm_cfg_t>;
  friend class detail::io::rx<rx_shm, rx_shm_cfg_t>;

  bool open_impl() {
    shm_ = std::make_shared<detail::shm_t>(opt_.file_path, opt_.size);
    return shm_->open() && shm_->map() != nullptr;
  }

  int recv_impl(char* data, size_t len) {
    if (!shm_) {
      errno = EBADF;
      return -1;
    }
    return shm_->readfrom(offset_, data, len);
  }

  void close_impl() {
    if (shm_) {
      shm_->close();
      shm_.reset();
    }
  }

 private:
  alignas(64) size_t offset_ = 0;
  char file_full_name_[256] = {0};
  std::shared_ptr<detail::shm_t> shm_;
};
}  // namespace net
