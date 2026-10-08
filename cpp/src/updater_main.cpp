#include "senzr/fota.hpp"
#include <csignal>
#include <iostream>

static senzr::fota::Client* g_client = nullptr;

static void handle_signal(int sig) {
    if (g_client) {
        std::cout << "\nReceived signal " << sig << ", stopping updater...\n";
        g_client->stop_daemon();
    }
}

int main() {
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    senzr::fota::Client client;
    g_client = &client;

    client.start_daemon();
    return 0;
}
