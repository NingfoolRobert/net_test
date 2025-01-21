

#include "../executor.h"

#include <thread>

void worker() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    printf("test value\n");
}

void worker_thread(detail::executor_t *exec) {
    while (true) {
        exec->loop(10);
    }
}

int main() {

    using namespace detail;
    executor_t exec;
    executor_context_t *context;

    std::thread thr(worker_thread, &exec);

    if (!exec.add_task(context, [&]() { worker(); })) {
        printf("add task fail\n");
        return 0;
    }
    //
    printf("add worker task.\n");
    std::this_thread::sleep_for(std::chrono::seconds(100));
    exec.rmv_task(context);
    printf("remove task worker success.\n");
    std::this_thread::sleep_for(std::chrono::seconds(100));

    std::vector<int> vtemp;
    vtemp.push_back(1);
    return 0;
}
