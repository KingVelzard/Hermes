#include "Reactor.h"
#include <thread>
#include <vector>
#include <functional>
#include <iostream>
#include <string>

constexpr int NUM_THREADS = 1;

int main(void) {

    std::function<void(int)> work = [](int worker_id) {
        Reactor reactor(worker_id);
        reactor.run();
    };

    std::vector<std::thread> workers;
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers.emplace_back(work, i);
    }
    for (int i = 0; i < NUM_THREADS; ++i) {
        workers[i].join();
    }

    return 0;
}
