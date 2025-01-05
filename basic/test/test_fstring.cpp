#include "../fstring.h"
#include <iostream>
#include <assert.h>

struct person_t  {
  detail::fstring<16>     name;
  int       age;
};

int main() {
    detail::fstring<1024> data;

    data.append("testdata", 10);

    std::cout << data.data() << std::endl;
    
    std::cout << data.length() << std::endl;
    
    std::cout << (data == "testdata") << std::endl;

    assert(sizeof(person_t) == 20);
    assert(sizeof(data) == 1024); 
    
    std::cout << data.substr(4, 3) << std::endl;
    
    std::cout << data.at(3) << std::endl;
    data[4] = 'm';
    std::cout << data.c_str() << std::endl;
    
    std::cout << data << std::endl;
    
    char ch = data[1024];
    return 0;
}
