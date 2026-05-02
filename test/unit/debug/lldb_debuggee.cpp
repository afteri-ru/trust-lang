// Отлаживаемая программа: рекурсивный factorial
#include <iostream>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main() {
    int r = factorial(3);
    std::cout << "factorial(5) = " << r << "\n";
    return 0;
}
