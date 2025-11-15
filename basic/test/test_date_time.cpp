#include "../date_time.h"

#include <gtest/gtest.h>

TEST(DateTime, NowAndFormat) 
{
  long now = detail::now();
  printf("%ld\n", now);
  
  std::string timestamp = detail::tick_to_timestamp(now, "%Y-%m-%d %H:%M:%S");
  printf("%s\n", timestamp.c_str());
  
  EXPECT_FALSE(timestamp.empty());
}
