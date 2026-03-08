#include "fix_gateway.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static FIXGateway* g_gateway = nullptr;

static void signal_handler(int) {
    if (g_gateway) g_gateway->stop();
}

int main(int argc, char* argv[]) {
    uint16_t port = 9000;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    FIXGateway gateway;
    g_gateway = &gateway;
    gateway.start(port);

    std::cout << "Gateway stopped.\n";
    return 0;
}
