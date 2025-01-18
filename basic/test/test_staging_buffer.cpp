#include "../staging_buffer.h"

#include <gtest/gtest.h>

TEST(staging_buffer,  allocate_test) {
  detail::StagingBuffer<128>  buffer;
  
  EXPECT_EQ(buffer.allocate(100) != nullptr,  true );
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
