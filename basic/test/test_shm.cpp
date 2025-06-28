#include "../fixed_shm.h"
#include <gtest/gtest.h>



TEST(fixed_shm, write_shm){

  detail::fixed_shm  shm("./test.shm",  1*1024*1024);
  shm.open();
  shm.map();

  auto  p = shm.mem(0);
  memcpy(p, "hello world", 12);
  
  
  auto s = shm.mem(0);
  
  ASSERT_STREQ(s, "hello world");
  
  remove("./test.shm");
}
