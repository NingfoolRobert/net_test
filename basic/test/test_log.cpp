#include "../logging.h"

#include "../optimization.h"
#include "../assert_macro.h"
#include "../date_time.h"



int main(){
  

  NLOG_DEBUG("%s", "hello world");
  NLOG_WARN("%s", "hello world");

  NLOG_WARN("test one");

  bool ret= true;
  assert_true(ret, 0);
  
  const char*  ptr = "hello";
  assert_nullptr(ptr,  0);


  auto now = detail::now();
  printf("%llu\n",now);
  return 0;
}
