#include "../flogging.h"



int main ()
{
  fmt::println("hello world {}", 123);
  FLOG_INFO("hello world {}", 123);
  FLOG_ERROR("hello world {}", 123);
  
  return 0;
}
