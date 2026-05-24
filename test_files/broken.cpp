#include <iostream>

int main() {
    int a = 10;
    
    // ERROR 1: 'b' is not declared in this scope
    // ERROR 2: Missing semicolon at the end
    b = a * 5 

    std::cout << b;

    // ERROR 3: Missing closing bracket for main()