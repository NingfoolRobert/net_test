#include "../bheap.h"

#include <iostream>
#include <gtest/gtest.h>



TEST(bheap,  INSERT_TEST)
{
  detail::bheap<int, std::less<int>> heap;
  detail::bheap<int, std::less<int>>::bheap_context*  context;
  heap.insert(10, context);
  
  EXPECT_EQ(heap.size(), 1);
  

}

TEST(bheap, TOP_TEST) {
  
  detail::bheap<int, std::less<int>> heap;
  detail::bheap<int, std::less<int>>::bheap_context*  context;
  heap.insert(8, context);
  heap.insert(10, context);
  EXPECT_EQ(heap.top(),  10);
}

TEST(bheap, ERASE) {
  detail::bheap<int, std::less<int>> heap;
  detail::bheap<int, std::less<int>>::bheap_context*  context;
  heap.insert(8, context);
  heap.insert(10, context);
  heap.insert(3, context);
  
  heap.erase(context->idx);
  EXPECT_EQ(heap.top(), 10);
  
}

TEST(bheap, ERASE1) {
  detail::bheap<int, std::less<int>> heap;
  detail::bheap<int, std::less<int>>::bheap_context*  context;
  heap.insert(8, context);
  heap.insert(10, context);
  heap.insert(11, context);
  
  heap.erase(context->idx);
  EXPECT_EQ(heap.top(), 10);
  EXPECT_EQ(heap.size(), 2);
  
}

TEST(bheap, POP) {
  detail::bheap<int, std::less<int>> heap;
  detail::bheap<int, std::less<int>>::bheap_context*  context;
  heap.insert(8, context);
  heap.insert(10, context);
  heap.insert(11, context);
  EXPECT_EQ(heap.top(), 11); 
  heap.pop();
  EXPECT_EQ(heap.top(), 10);
  heap.pop();
  EXPECT_EQ(heap.top(), 8);
  
}


