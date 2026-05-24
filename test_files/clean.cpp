#include <iostream>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main() {
    int result = factorial(5);
    std::cout << "Factorial of 5 is: " << result << std::endl;
    return 0;
}