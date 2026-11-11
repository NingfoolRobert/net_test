#include "../staging_buffer.h"

#include <gtest/gtest.h>

TEST(staging_buffer,  allocate_test) {
  detail::StagingBuffer<128>  buffer;
  
  EXPECT_EQ(buffer.allocate(100) != nullptr,  true );
}

TEST(staging_buffer,  allocate_test1) {
  detail::StagingBuffer<128>  buffer;
  EXPECT_EQ(buffer.allocate(128, false),  nullptr);
}

TEST(staging_buffer,  allocate_test2) {
  detail::StagingBuffer<128>  buffer;
  EXPECT_EQ(buffer.allocate(256, false),  nullptr);
}

TEST(staging_buffer,  allocate_test3) {
  detail::StagingBuffer<128>  buffer;
  EXPECT_NE(buffer.allocate(64),  nullptr);
  buffer.deallocate(64);
  EXPECT_NE(buffer.allocate(64),  nullptr);
  buffer.deallocate(64);
  EXPECT_NE(buffer.allocate(64),  nullptr);
  buffer.deallocate(64);
}



//int main() {
//  
//
//
//  detail::StagingBuffer<128>  buffer;
//
//  auto ptr = buffer.allocate(100);
//
//
//  buffer.deallocate(100);
//  
//  ptr =  buffer.allocate(128);
//  buffer.deallocate(128);
//
//
//  return 0;
//
//
//}
