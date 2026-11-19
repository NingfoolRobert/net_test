/**
 * @file rx_pcap_tcp.h
 * @author bfning
 * @brief
 * @version 0.1
 * @date 2026-09-09
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../../common/tcp_stream_manager.h"

struct pcap;

namespace net {

struct rx_pcap_tcp_cfg_t {
  std::string file_path;
  // Exact directional allowlist, in host byte order. Empty denies all.
  std::vector<tcp_match_rule> rules;
  tcp_stream_manager_config streams;
  uint32_t domain_id = 0;
  // Offloaded outbound captures may contain unfinished checksums.
  bool verify_checksums = false;
};

// Offline IPv4/TCP capture in one capture domain, owned by one thread.
// IPv4 fragments are skipped; only TCP byte streams are reassembled.
class rx_pcap_tcp {
 public:
  rx_pcap_tcp();
  ~rx_pcap_tcp();
  rx_pcap_tcp(const rx_pcap_tcp&) = delete;
  rx_pcap_tcp& operator=(const rx_pcap_tcp&) = delete;
  rx_pcap_tcp(rx_pcap_tcp&&) = delete;
  rx_pcap_tcp& operator=(rx_pcap_tcp&&) = delete;

  [[nodiscard]] const rx_pcap_tcp_cfg_t& config() const { return cfg_; }
  // Configuration changes take effect on the next open().
  void set_config(const rx_pcap_tcp_cfg_t& cfg) { cfg_ = cfg; }

  bool open();
  void close();

  // >0: contiguous bytes from one directed stream; -2: EOF; -1: error.
  // A short destination retains the suffix for the next read. read() omits
  // stream identity and is intended for a single directed rule.
  int read(char* data, int len);
  int read_from(tcp_stream_context& context, char* data, int len);

  // One parsed TCP packet per call: 1 even when filtered or control-only,
  // -2 on EOF, -1 on capture/resource error (see error()). Malformed packets
  // and IPv4 fragments are skipped. Inspect last_result() for
  // manager admission results. Callbacks have the manager's signatures.
  // Finish any short read before switching to dispatch().
  // Do not reenter or close the receiver from callbacks.
  template <typename DataFn, typename StateFn>
  int dispatch(DataFn&& on_data, StateFn&& on_state) {
    const call_guard guard(busy_);
    if (pending_) {
      throw std::logic_error("Drain the pending TCP read before dispatch");
    }
    tcp_packet_view packet;
    const int result = next_packet(packet);
    if (result != 1) {
      return result;
    }
    last_result_ = manager_->insert(packet, std::forward<DataFn>(on_data),
                                    std::forward<StateFn>(on_state));
    return insert_result();
  }

  // Borrowed until close()/open(). EOF calls finish_all(), preserving buffered
  // data. The caller controls gap skipping and release of terminated records.
  tcp_stream_manager* streams() { return manager_.get(); }
  [[nodiscard]] const tcp_stream_manager* streams() const {
    return manager_.get();
  }
  [[nodiscard]] const tcp_manager_insert_result& last_result() const {
    return last_result_;
  }
  [[nodiscard]] const std::string& error() const { return error_; }

 private:
  class call_guard {
   public:
    explicit call_guard(bool& busy);
    ~call_guard();
    call_guard(const call_guard&) = delete;
    call_guard& operator=(const call_guard&) = delete;
    call_guard(call_guard&&) = delete;
    call_guard& operator=(call_guard&&) = delete;

   private:
    bool& busy_;
  };

  int next_packet(tcp_packet_view& packet);
  int insert_result();
  void reset();

  rx_pcap_tcp_cfg_t cfg_;
  rx_pcap_tcp_cfg_t active_cfg_;
  ::pcap* pcap_ = nullptr;
  std::unique_ptr<tcp_stream_manager> manager_;
  std::optional<tcp_connection_handle> pending_;
  tcp_manager_insert_result last_result_;
  std::string error_;
  int datalink_ = 0;
  int source_result_ = 0;
  bool busy_ = false;
};
}  // namespace net
