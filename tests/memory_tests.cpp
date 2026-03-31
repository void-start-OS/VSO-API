// tests/memory_tests.cpp
#include <vsoapi.hpp>
#include <cstdio>

int main() {
    using namespace vso;

    printf("[Memory Test] Start\n");

    void* ptr = memory::Alloc(256);
    if (!ptr) {
        printf("Alloc failed\n");
        return 1;
    }

    printf("Allocated 256 bytes at %p\n", ptr);

    memory::Free(ptr);
    printf("Freed memory\n");

    printf("[Memory Test] Done\n");
    return 0;
}

