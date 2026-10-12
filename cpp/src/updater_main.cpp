#include "app/updater.hpp"
#include <csignal>
#include <iostream>

static app::OtaUpdater* g_updater = nullptr;

static void handle_signal(int sig) {
    if (g_updater) {
        std::cout << "\nReceived signal " << sig << ", stopping updater...\n";
        g_updater->stop();
    }
}

int main() {
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    app::OtaUpdater updater;
    g_updater = &updater;

    updater.run();
    return 0;
}
