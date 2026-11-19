#include "../basic/ipc/ipc_lock.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>

int main() {
    const std::string path = "/tmp/ipc_lock_test_" + std::to_string(::getpid());
    std::remove(path.c_str());

    ipc_lock parent_lock(path);
    std::unique_lock<ipc_lock> parent_guard(parent_lock, std::defer_lock);
    assert(parent_guard.try_lock());
    assert(parent_lock.owns_lock());

    int ready[2] = {-1, -1};
    assert(::pipe(ready) == 0);
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        ::close(ready[0]);
        ipc_lock child_lock(path);
        if (child_lock.try_lock()) {
            _exit(2);
        }
        const char done = 1;
        assert(::write(ready[1], &done, 1) == 1);
        ::close(ready[1]);
        child_lock.lock();
        child_lock.unlock();
        _exit(0);
    }

    ::close(ready[1]);
    char done = 0;
    assert(::read(ready[0], &done, 1) == 1);
    ::close(ready[0]);
    parent_guard.unlock();

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::remove(path.c_str());
    return 0;
}
#else
int main() { return 0; }
#endif
