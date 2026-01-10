#include "../async_result_syncer.h"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

int num = 0;

class AsyncResultSyncerTest : public ::testing::Test {
  protected:
  void SetUp() override {
    // Code here will be called immediately after the constructor (right
    // before each test).
  }

  void TearDown() override {
    // Code here will be called immediately after each test (right
    // before the destructor).
  }
};

TEST_F(AsyncResultSyncerTest, Test) {

  // detail::atrs_t<bool> atrs([](bool &res) -> bool { return res; });
  auto atrs = detail::make_atrs<bool>([](bool &res) { return res; });
  std::thread thr([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    atrs.notify(1, num == 0);
  });
  thr.detach();
  EXPECT_EQ(atrs.wait_for(1, std::chrono::seconds(5)), true);
}