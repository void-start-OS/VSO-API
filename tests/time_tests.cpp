// tests/time_tests.cpp
#include <vsoapi.hpp>
#include <cstdio>

int main() {
    using namespace vso;

    printf("[Time Test] Start\n");

    auto before = time::GetTickCount();
    printf("Tick before: %llu ms\n", before.milliseconds);

    time::Sleep(500);

    auto after = time::GetTickCount();
    printf("Tick after: %llu ms\n", after.milliseconds);

    printf("Elapsed: %llu ms\n", after.milliseconds - before.milliseconds);

    printf("[Time Test] Done\n");
    return 0;
}
