#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <random>
int main() {
    std::size_t iters = 1000000;
    std::vector<double> lats(iters);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1000, 99000);
    for (std::size_t i = 0; i < iters; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        int v = dist(rng);
        asm volatile("" : : "r"(v) : "memory");
        auto end = std::chrono::high_resolution_clock::now();
        lats[i] = std::chrono::duration<double, std::nano>(end - start).count();
    }
    std::sort(lats.begin(), lats.end());
    std::printf("p50: %.1f, p99: %.1f, p99.9: %.1f\n", lats[iters/2], lats[iters*99/100], lats[iters*999/1000]);
    return 0;
}
