#include <hestia/config.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace hestia::config {
    namespace {
        std::optional<std::filesystem::path> env_path(const char *name) {
            if (const char *value = std::getenv(name); value && *value) {
                return std::filesystem::path(value);
            }
            return std::nullopt;
        }

        // Platform default data directory:
        //   Windows:    %APPDATA%\Hestia  (fallback %USERPROFILE%\AppData\Roaming\Hestia)
        //   Unix (*nix/macOS): ~/.hestia
        std::filesystem::path platform_data_home() {
#if defined(_WIN32)
            if (const auto appdata = env_path("APPDATA")) {
                return *appdata / "Hestia";
            }
            if (const auto profile = env_path("USERPROFILE")) {
                return *profile / "AppData" / "Roaming" / "Hestia";
            }
#else
            if (const auto home = env_path("HOME")) {
                return *home / ".hestia";
            }
#endif
            return std::filesystem::current_path();
        }

        // Pointer file recording a user-chosen data directory, kept at the anchor
        // so it is always found regardless of where data is redirected.
        std::filesystem::path pointer_file() {
            return platform_data_home() / "home";
        }

        std::optional<std::filesystem::path> read_pointer() {
            std::ifstream in(pointer_file());
            if (!in) {
                return std::nullopt;
            }
            std::string line;
            while (std::getline(in, line)) {
                // Tolerate trailing CR (Windows line endings) and whitespace.
                while (!line.empty() &&
                       (line.back() == '\r' || line.back() == '\n' ||
                        line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }
                if (!line.empty()) {
                    return std::filesystem::path(line);
                }
            }
            return std::nullopt;
        }
    }

    std::filesystem::path anchor_dir() {
        return platform_data_home();
    }

    std::filesystem::path data_home(const std::filesystem::path &override_dir) {
        if (!override_dir.empty()) {
            return override_dir;
        }
        if (const auto env = env_path("HESTIA_HOME")) {
            return *env;
        }
        if (const auto pointer = read_pointer()) {
            return *pointer;
        }
        return platform_data_home();
    }

    void set_persisted_home(const std::filesystem::path &dir) {
        const auto pointer = pointer_file();
        if (dir.empty()) {
            std::error_code ec;
            std::filesystem::remove(pointer, ec);
            return;
        }
        if (pointer.has_parent_path()) {
            std::filesystem::create_directories(pointer.parent_path());
        }
        std::ofstream out(pointer, std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to write home pointer: " +
                                     pointer.string());
        }
        out << dir.string() << '\n';
    }

    std::filesystem::path config_path(const std::filesystem::path &override_dir) {
        return data_home(override_dir) / "config";
    }

    Config Config::load(const std::filesystem::path &path) {
        Config config;
        std::ifstream in(path);
        if (!in) {
            return config;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line.front() == '#') {
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            config.entries_.insert_or_assign(line.substr(0, eq), line.substr(eq + 1));
        }
        return config;
    }

    std::optional<std::string> Config::get(std::string_view key) const {
        if (const auto it = entries_.find(std::string(key)); it != entries_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void Config::set(std::string_view key, std::string_view value) {
        // The on-disk format is one `key=value` line each, so a key/value
        // carrying a newline (or a key carrying '=') would corrupt the file and
        // mis-parse on load. Reject rather than silently mangle.
        if (key.empty()) {
            throw std::invalid_argument("config key must not be empty");
        }
        if (key.find_first_of("=\n\r") != std::string_view::npos) {
            throw std::invalid_argument("config key must not contain '=', newline, or CR");
        }
        if (value.find_first_of("\n\r") != std::string_view::npos) {
            throw std::invalid_argument("config value must not contain newline or CR");
        }
        entries_.insert_or_assign(std::string(key), std::string(value));
    }

    void Config::save(const std::filesystem::path &path) const {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path, std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to open config file for writing: " +
                                     path.string());
        }
        for (const auto &[key, value] : entries_) {
            out << key << '=' << value << '\n';
        }
    }
}
