#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <hestia/config.h>

namespace hestia::daemon {
    // The daemon's view of configuration. Unlike the old per-invocation CLI model,
    // the data directory is daemon-global: resolved once at startup (and again on
    // set_home) from $HESTIA_HOME / the persisted pointer / the platform default.
    // The daemon serves one request at a time, so no locking is needed.
    class ConfigService {
    public:
        ConfigService() { reload(); }

        std::optional<std::string> get(const std::string &key) const { return cfg_.get(key); }

        void set(const std::string &key, const std::string &value) {
            cfg_.set(key, value);
            cfg_.save(path_);
        }

        std::filesystem::path home() const { return home_; }

        // Persist a new data directory and re-resolve immediately so the change
        // takes effect for this running daemon, not just the next start.
        std::filesystem::path set_home(const std::string &dir) {
            config::set_persisted_home(dir);
            reload();
            return home_;
        }

    private:
        void reload() {
            home_ = config::data_home();
            path_ = config::config_path();
            cfg_ = config::Config::load(path_);
        }

        std::filesystem::path home_;
        std::filesystem::path path_;
        config::Config cfg_;
    };
}
