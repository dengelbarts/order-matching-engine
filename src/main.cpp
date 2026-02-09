#include <iostream>
#include <string>

int main() {
    std::cout << "Order Matching Engine v0.1.0" << std::endl;
    std::cout << "Build system verification successful!" << std::endl;
    
    // Verify C++17 features compile
    std::string message = "C++17 features enabled";
    if (auto len = message.length(); len > 0) {
        std::cout << "Init statement in if: " << message << std::endl;
    }
    
    return 0;
}