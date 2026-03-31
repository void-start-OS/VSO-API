// tests/console_tests.cpp
#include <vsoapi.hpp>
#include <cstdio>

int main() {
    using namespace vso;

    printf("[Console Test] Start\n");

    console::WriteLine("Hello from VSOAPI Console!");
    console::Write("No newline...");
    console::WriteLine(" <- now newline");

    console::SetTextColor(ConsoleColor::LightGreen);
    console::WriteLine("This should be green text.");
    console::SetTextColor(ConsoleColor::Default);

    console::Clear();
    console::WriteLine("Screen cleared.");

    printf("[Console Test] Done\n");
    return 0;
}
