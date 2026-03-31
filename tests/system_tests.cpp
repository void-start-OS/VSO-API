// tests/system_tests.cpp
#include <vsoapi.hpp>
#include <cstdio>

int main() {
    using namespace vso;

    printf("[System Test] Start\n");

    system::SystemInfo info{};
    if (system::GetSystemInfo(info) == Result::Ok) {
        printf("Total Memory: %llu bytes\n", info.totalMemoryBytes);
        printf("Used  Memory: %llu bytes\n", info.usedMemoryBytes);
        printf("CPU Count:    %u\n", info.cpuCount);
    } else {
        printf("GetSystemInfo failed\n");
    }

    // Reboot は危険なのでコメントアウト
    // system::Reboot();

    printf("[System Test] Done\n");
    return 0;
}
