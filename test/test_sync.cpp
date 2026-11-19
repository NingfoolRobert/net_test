#include "../core/synchronizer.h"
#include <thread>
#include <chrono>
#include <iostream>

Synchronizer<int> syncer;

void worker(int value){
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    syncer.notify(value);
}


int main() {
    
    std::thread thr(worker, 19);
    syncer.wait(3000);
    //std::cout << syncer.get() << std::endl;
    thr.join();
    
    return 0;
}
