#include "../date_time.h"



int main() 
{
  printf("%ld\n", detail::now());
  
  printf("%s\n", detail::tick_to_timestamp(detail::now(), "%Y-%m-%d %H:%M:%S").c_str());
  return 0;
}
