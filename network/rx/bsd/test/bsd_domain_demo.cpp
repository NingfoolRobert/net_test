#include "bsd_domain.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool send_all(net::bsd_domain_t &connection, const char *data, size_t len) {
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

bool recv_line(net::bsd_domain_t &connection, std::string *line) {
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
            << "  " << program << " server [socket-path]\n"
            << "  " << program << " client [socket-path] [message]\n"
            << "Default socket path: /tmp/bsd_domain_demo.sock\n";
}

int run_server(const char *socket_path) {
  net::bsd_domain_cfg_t cfg;
  std::strncpy(cfg.sock_path, socket_path, sizeof(cfg.sock_path) - 1);

  net::bsd_domain_t listener(cfg);
  if (!listener.bind()) {
    std::cerr << "bind/listen failed: " << std::strerror(errno) << '\n';
    return 1;
  }
  std::cout << "Listening on " << socket_path << " ...\n";

  std::unique_ptr<net::bsd_domain_t> connection(listener.accept());
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

int run_client(const char *socket_path, const char *text) {
  if (std::strchr(text, '\n') != nullptr) {
    std::cerr << "message must be a single line\n";
    return 2;
  }

  net::bsd_domain_cfg_t cfg;
  std::strncpy(cfg.sock_path, socket_path, sizeof(cfg.sock_path) - 1);

  net::bsd_domain_t connection(cfg);
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
  if (argc < 2 || (std::strcmp(argv[1], "server") != 0 && std::strcmp(argv[1], "client") != 0)) {
    print_usage(argv[0]);
    return 2;
  }

  const bool is_server = std::strcmp(argv[1], "server") == 0;
  const char *socket_path = argc > 2 ? argv[2] : "/tmp/bsd_domain_demo.sock";
  net::bsd_domain_cfg_t path_cfg;
  if (std::strlen(socket_path) >= sizeof(path_cfg.sock_path)) {
    std::cerr << "socket path is too long\n";
    return 2;
  }

  if (is_server) {
    return run_server(socket_path);
  }
  const char *message = argc > 3 ? argv[3] : "hello from bsd_domain";
  return run_client(socket_path, message);
}
