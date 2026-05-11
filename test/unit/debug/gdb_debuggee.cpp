// Отлаживаемая программа для GDB: рекурсивный factorial
#include <iostream>

int factorial(int n) {
    if (n <= 1)
        return 1;
    return n * factorial(n - 1);
}

int main() {
    int r = factorial(5);
    std::cout << "gdb_factorial(5) = " << r << "\n";
    return 0;
}