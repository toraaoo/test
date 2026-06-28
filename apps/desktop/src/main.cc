#include <spdlog/spdlog.h>

#include <hestia/logging.h>

int main(int argc, char *argv[]) {
    hestia::init_logging();
    spdlog::info("Hestia desktop starting");
    return 0;
}
