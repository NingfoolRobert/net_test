#include "../rx.h"
#include <string.h>

struct test_rx_cfg {
    char ip[16];
    unsigned short port;
};
class test_rx;

template<>
  struct detail::io::traist_type<test_rx>
{
  using opt_t = test_rx_cfg;
};

class test_rx :
    public detail::io::rx<test_rx>

{
public:
    //
    bool open() {
        printf("open: %s\n", opt_.ip);
        return true;
    }

    void close() {
        printf("close\n");
    }

    int read(char *data, int len) {
        return 0;
    }
};

int main() {

    {

        detail::io::rx<test_rx> test;

        auto &opt = test.get_opt();
        strcpy(opt.ip, "192.168.1.1");
        test.open();
    }
    
    //
    return 0;
}
