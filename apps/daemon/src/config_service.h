#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

#include <hestia/config.h>

namespace hestia::daemon {
    // The daemon's view of configuration. The data directory is daemon-global:
    // resolved at startup (and on set_home) from $HESTIA_HOME / the persisted
    // pointer / the platform default. Access is guarded for concurrent clients.
    class ConfigService {
    public:
        ConfigService() { reload(); }

        std::optional<std::string> get(const std::string &key) const {
            std::lock_guard<std::mutex> lk(mu_);
            return cfg_.get(key);
        }

        void set(const std::string &key, const std::string &value) {
            std::lock_guard<std::mutex> lk(mu_);
            cfg_.set(key, value);
            cfg_.save(path_);
        }

        std::filesystem::path home() const {
            std::lock_guard<std::mutex> lk(mu_);
            return home_;
        }

        // Persist a new data directory and re-resolve immediately so the change
        // takes effect for this running daemon, not just the next start.
        std::filesystem::path set_home(const std::string &dir) {
            std::lock_guard<std::mutex> lk(mu_);
            config::set_persisted_home(dir);
            reload();
            return home_;
        }

    private:
        // Caller holds mu_.
        void reload() {
            home_ = config::data_home();
            path_ = config::config_path();
            cfg_ = config::Config::load(path_);
        }

        mutable std::mutex mu_;
        std::filesystem::path home_;
        std::filesystem::path path_;
        config::Config cfg_;
    };
}
