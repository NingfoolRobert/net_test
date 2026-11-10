#include <gtest/gtest.h>
#include <unistd.h>

#include <cstring>
#include <type_traits>

#include "../../../network/io/bsd/rx_bsd_mcbc.h"
#include "../../../network/io/bsd/tx_bsd_mcbc.h"
#include "../../../network/io/shm/rx_shm.h"

TEST(BasicBsdIo, UnopenedMulticastSocketsReportBadDescriptor) {
  net::rx_bsd_mcbc receiver;
  net::tx_bsd_mcbc sender;
  char data[4];
  EXPECT_EQ(receiver.recv(data, sizeof(data)), -1);
  EXPECT_EQ(receiver.last_error(), EBADF);
  EXPECT_EQ(sender.send("test", 4), -1);
  EXPECT_EQ(sender.last_error(), EBADF);
}
TEST(BasicShmIo, ReadsMappedFileAndReportsErrorAfterClose) {
  char path[] = "/tmp/basic-io-shm-XXXXXX";
  int fd = ::mkstemp(path);
  ASSERT_GE(fd, 0);
  struct Cleanup {
    int fd;
    const char* path;
    ~Cleanup() {
      ::close(fd);
      ::unlink(path);
    }
  } cleanup{fd, path};
  ASSERT_EQ(::ftruncate(fd, 4096), 0);
  ASSERT_EQ(::pwrite(fd, "hello", 5, 0), 5);
  net::rx_shm_cfg_t cfg{};
  cfg.size = 4096;
  std::strcpy(cfg.file_path, path);
  net::rx_shm receiver(net::rx_shm_cfg_t{});
  receiver.set_opt(cfg);
  char data[5];
  EXPECT_EQ(receiver.recv(data, sizeof(data)), -1);
  EXPECT_EQ(receiver.last_error(), EBADF);
  ASSERT_TRUE(receiver.open());
  ASSERT_EQ(receiver.recv(data, sizeof(data)), 5);
  EXPECT_EQ(std::string(data, sizeof(data)), "hello");
  EXPECT_EQ(receiver.last_error(), 0);
  receiver.close();
  EXPECT_EQ(receiver.last_error(), 0);
  EXPECT_EQ(receiver.recv(data, sizeof(data)), -1);
  EXPECT_EQ(receiver.last_error(), EBADF);
}

TEST(BasicShmIo, ConstructorAndLegacyConfigurationUseInheritedOptions) {
  const net::rx_shm_cfg_t initial{4096};
  net::rx_shm receiver(initial);
  EXPECT_EQ(receiver.get_opt().size, initial.size);
  EXPECT_EQ(&receiver.get_cfg(), &receiver.get_opt());
  const net::rx_shm& const_receiver = receiver;
  EXPECT_EQ(&const_receiver.get_cfg(), &const_receiver.get_opt());
  const net::rx_shm_cfg_t replacement{8192};
  receiver.set_cfg(replacement);
  EXPECT_EQ(receiver.get_opt().size, replacement.size);
  receiver.set_opt(initial);
  EXPECT_EQ(receiver.get_cfg().size, initial.size);
  static_assert(!std::is_copy_constructible_v<net::rx_shm>);
}
