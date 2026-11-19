#include "bsd_mcbc.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

bool parse_port(const char *text, uint16_t *port) {
  char *end = nullptr;
  errno = 0;
  const unsigned long value = std::strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value == 0 || value > 65535) {
    return false;
  }
  *port = static_cast<uint16_t>(value);
  return true;
}

void print_usage(const char *program) {
  std::cerr << "Usage:\n"
            << "  " << program << " recv [group-ip] [port] [interface-ip]\n"
            << "  " << program << " send [group-ip] [port] [interface-ip] [message]\n"
            << "Defaults: group-ip=239.1.1.1, port=3000, interface-ip=0.0.0.0\n";
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2 || (std::strcmp(argv[1], "recv") != 0 && std::strcmp(argv[1], "send") != 0)) {
    print_usage(argv[0]);
    return 2;
  }

  const bool is_receiver = std::strcmp(argv[1], "recv") == 0;
  const char *group_ip = argc > 2 ? argv[2] : "239.1.1.1";
  const char *interface_ip = argc > 4 ? argv[4] : "0.0.0.0";
  uint16_t port = 3000;
  if ((argc > 3 && !parse_port(argv[3], &port)) || !net::util::is_multicast_ipv4(group_ip) ||
      (std::strcmp(interface_ip, "0.0.0.0") != 0 && !net::util::is_ipv4_addr(interface_ip))) {
    print_usage(argv[0]);
    return 2;
  }

  net::bsd_mcbc_cfg_t cfg;
  cfg.remote_addr.set_ip(group_ip);
  cfg.remote_addr.set_port(port);
  cfg.local_addr.set_ip("0.0.0.0");
  cfg.local_addr.set_port(is_receiver ? port : 0);
  cfg.is_multicast = true;
  std::strncpy(cfg.interface, interface_ip, sizeof(cfg.interface) - 1);

  net::bsd_mcbc_t endpoint(cfg);
  const bool opened = is_receiver ? endpoint.bind() : endpoint.connect();
  if (!opened) {
    std::cerr << "Failed to " << (is_receiver ? "bind" : "connect") << " UDP multicast endpoint: "
              << std::strerror(errno) << '\n';
    return 1;
  }

  if (!is_receiver) {
    const char *message = argc > 5 ? argv[5] : "hello from bsd_mcbc";
    const int sent = endpoint.send(message, static_cast<int>(std::strlen(message)));
    if (sent < 0) {
      std::cerr << "send failed: " << std::strerror(errno) << '\n';
      return 1;
    }
    std::cout << "Sent " << sent << " bytes to " << group_ip << ':' << port << '\n';
    return 0;
  }

  char data[2048];
  const int received = endpoint.recv(data, static_cast<int>(sizeof(data)));
  if (received < 0) {
    std::cerr << "recv failed: " << std::strerror(errno) << '\n';
    return 1;
  }
  std::cout << "Received " << received << " UDP payload bytes from " << group_ip << ':' << port << ": ";
  std::cout.write(data, received);
  std::cout << '\n';
  return 0;
}
