#include "../staging_buffer.h"
#include <string.h>

int main() {

    using namespace detail;

    StagingBuffer<128> alloc;

    for (auto i = 0; i < 10; ++i) {

        auto ptr = alloc.allocate(100);
        memcpy(ptr, "101202011", 10);

        alloc.deallocate(100);

        ptr = alloc.allocate(127);

        alloc.deallocate(127);

        ptr = alloc.allocate(100);

        alloc.deallocate(100);
    }

    return 0;
}
