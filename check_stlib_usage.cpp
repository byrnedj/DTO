#include <iostream>
#include <cstring>

int main() {
    char source[] = "Hello, World!";
    char destination[20];

    // Copy the contents of 'source' to 'destination'
    std::memcpy(destination, source, std::strlen(source) + 1); 

    std::cout << "Source: " << source << std::endl;
    std::cout << "Destination: " << destination << std::endl;

    return 0;
}