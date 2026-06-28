#include <iostream>

#include <CLI/CLI.hpp>

#include <hestia/logging.h>

#include "tui.h"

#ifndef HESTIA_VERSION
#define HESTIA_VERSION "0.0.0"
#endif

int main(int argc, char **argv) {
    CLI::App app{"Hestia command-line interface"};
    app.set_version_flag("--version", HESTIA_VERSION);

    bool verbose = false;
    bool quiet = false;
    auto *verbose_flag = app.add_flag("-v,--verbose", verbose,
                                      "Enable verbose (debug) logging");
    app.add_flag("-q,--quiet", quiet,
                 "Only show warnings and errors")
        ->excludes(verbose_flag);

    auto *tui = app.add_subcommand("tui", "Launch the interactive terminal UI");

    // Further subcommands (features) get registered here.

    CLI11_PARSE(app, argc, argv);

    hestia::LogLevel level = hestia::LogLevel::info;
    if (verbose) {
        level = hestia::LogLevel::debug;
    } else if (quiet) {
        level = hestia::LogLevel::warn;
    }
    hestia::init_logging(level);

    if (tui->parsed()) {
        return hestia::cli::run_tui();
    }

    // No subcommand: show usage.
    std::cout << app.help();
    return 0;
}
