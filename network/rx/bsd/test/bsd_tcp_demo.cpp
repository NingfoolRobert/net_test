#include "bsd_tcp.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

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

bool send_all(net::bsd_tcp_t &connection, const char *data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    const int count = connection.send(data + sent, static_cast<int>(len - sent));
    if (count <= 0) {
      return false;
    }
    sent += static_cast<size_t>(count);
  }
  return true;
}

bool recv_line(net::bsd_tcp_t &connection, std::string *line) {
  line->clear();
  while (line->size() < 2048) {
    char ch = '\0';
    const int count = connection.recv(&ch, 1);
    if (count <= 0) {
      return false;
    }
    if (ch == '\n') {
      return true;
    }
    line->push_back(ch);
  }
  return false;
}

void print_usage(const char *program) {
  std::cerr << "Usage:\n"
            << "  " << program << " server [bind-ip] [port]\n"
            << "  " << program << " client [server-ip] [port] [message]\n"
            << "Defaults: server bind-ip=0.0.0.0, client server-ip=127.0.0.1, port=39000\n";
}

int run_server(const char *bind_ip, uint16_t port) {
  net::bsd_tcp_cfg_t cfg;
  cfg.local_addr.set_ip(bind_ip);
  cfg.local_addr.set_port(port);

  net::bsd_tcp_t listener(cfg);
  if (!listener.bind()) {
    std::cerr << "bind/listen failed: " << std::strerror(errno) << '\n';
    return 1;
  }
  std::cout << "Listening on " << bind_ip << ':' << port << " ...\n";

  std::unique_ptr<net::bsd_tcp_t> connection(listener.accept());
  if (!connection) {
    std::cerr << "accept failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  std::string message;
  if (!recv_line(*connection, &message)) {
    std::cerr << "recv failed or client closed before sending a line\n";
    return 1;
  }
  std::cout << "Received: " << message << '\n';

  message.push_back('\n');
  if (!send_all(*connection, message.data(), message.size())) {
    std::cerr << "echo send failed: " << std::strerror(errno) << '\n';
    return 1;
  }
  return 0;
}

int run_client(const char *server_ip, uint16_t port, const char *text) {
  if (std::strchr(text, '\n') != nullptr) {
    std::cerr << "message must be a single line\n";
    return 2;
  }

  net::bsd_tcp_cfg_t cfg;
  cfg.remote_addr.set_ip(server_ip);
  cfg.remote_addr.set_port(port);

  net::bsd_tcp_t connection(cfg);
  if (!connection.connect()) {
    std::cerr << "connect failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  std::string message(text);
  message.push_back('\n');
  if (!send_all(connection, message.data(), message.size())) {
    std::cerr << "send failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  std::string echoed;
  if (!recv_line(connection, &echoed)) {
    std::cerr << "recv failed or server closed before replying\n";
    return 1;
  }
  std::cout << "Echo: " << echoed << '\n';
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 2;
  }

  const bool is_server = std::strcmp(argv[1], "server") == 0;
  const bool is_client = std::strcmp(argv[1], "client") == 0;
  if (!is_server && !is_client) {
    print_usage(argv[0]);
    return 2;
  }

  const char *ip = argc > 2 ? argv[2] : (is_server ? "0.0.0.0" : "127.0.0.1");
  uint16_t port = 39000;
  if ((argc > 3 && !parse_port(argv[3], &port)) || !net::util::is_ipv4_addr(ip)) {
    print_usage(argv[0]);
    return 2;
  }

  if (is_server) {
    return run_server(ip, port);
  }
  const char *message = argc > 4 ? argv[4] : "hello from bsd_tcp";
  return run_client(ip, port, message);
}
