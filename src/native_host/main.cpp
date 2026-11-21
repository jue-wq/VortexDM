#include "native_messaging_host.hpp"

#include <iostream>

int main() {
    vortex::NativeMessagingHost host;
    return host.run(std::cin, std::cout);
}
