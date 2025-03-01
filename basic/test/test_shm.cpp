#include "../fixed_shm.h"
#include <gtest/gtest.h>



TEST(fixed_shm, write_shm){

  detail::fixed_shm  shm("./test.shm",  1*1024*1024);
  shm.open();
  shm.map();

  auto  p = shm.mem(shm.length());
  memcpy(p, "hello world", 12);
  shm.peek(12);
  
  EXPECT_EQ(shm.length(), 12 + sizeof(detail::fixed_shm::meta_t));
  
  auto s = shm.mem(sizeof(detail::fixed_shm::meta_t));
  
  ASSERT_STREQ(s, "hello world");
}
