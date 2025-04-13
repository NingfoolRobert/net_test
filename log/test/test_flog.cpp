#include "../flogging.h"



int main ()
{
  INIT_FLOG(LogLevelEnum::LOG_LEVEL_DEBUG,  "./", "example.log");
  fmt::println("hello world {}", 123);
  FLOG_INFO("hello world {}", 123);
  FLOG_ERROR("hello world {}", 123);
  
  return 0;
}
