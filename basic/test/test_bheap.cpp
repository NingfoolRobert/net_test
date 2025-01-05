#include "../bheap.h"

#include <iostream>





int main() {
  
  detail::bheap<int, std::less<int>>  heap;
  detail::bheap<int, std::less<int>>::bheap_context*  context;
  
  heap.insert(10, context);
  heap.insert(11, context);
  heap.insert(8, context);
  heap.insert(9, context);
  heap.insert(20, context);
  
  heap.erase(context->idx);

  return 0;
}
