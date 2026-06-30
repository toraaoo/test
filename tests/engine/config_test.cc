#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <hestia/config.h>

namespace fs = std::filesystem;
using hestia::config::Config;

namespace {
    void set_env(const char *key, const char *value) {
#if defined(_WIN32)
        _putenv_s(key, value ? value : "");
#else
        if (value)
            ::setenv(key, value, 1);
        else
            ::unsetenv(key);
#endif
    }
}

TEST(Config, MissingFileIsEmpty) {
    const fs::path path = fs::temp_directory_path() / "hestia_test_absent" / "config";
    fs::remove_all(path.parent_path());
    const Config cfg = Config::load(path);
    EXPECT_FALSE(cfg.get("anything").has_value());
}

TEST(Config, SetSaveLoadRoundTrip) {
    const fs::path dir = fs::temp_directory_path() / "hestia_test_cfg";
    const fs::path path = dir / "config";
    fs::remove_all(dir);

    Config cfg = Config::load(path);
    cfg.set("theme", "dark");
    cfg.set("name", "Ada");
    cfg.save(path);

    const Config reloaded = Config::load(path);
    EXPECT_EQ(reloaded.get("theme").value_or(""), "dark");
    EXPECT_EQ(reloaded.get("name").value_or(""), "Ada");
    EXPECT_FALSE(reloaded.get("missing").has_value());

    fs::remove_all(dir);
}

TEST(Config, SetOverwrites) {
    Config cfg;
    cfg.set("k", "one");
    cfg.set("k", "two");
    EXPECT_EQ(cfg.get("k").value_or(""), "two");
}

TEST(Config, SetRejectsCorruptingCharacters) {
    Config cfg;
    EXPECT_THROW(cfg.set("", "v"), std::invalid_argument);
    EXPECT_THROW(cfg.set("a=b", "v"), std::invalid_argument);
    EXPECT_THROW(cfg.set("a\nb", "v"), std::invalid_argument);
    EXPECT_THROW(cfg.set("k", "line1\nline2"), std::invalid_argument);
    EXPECT_THROW(cfg.set("k", "has\rcr"), std::invalid_argument);
    // A value may contain '=' — only the key splits on it.
    EXPECT_NO_THROW(cfg.set("url", "http://x?a=b"));
    EXPECT_EQ(cfg.get("url").value_or(""), "http://x?a=b");
}

TEST(Config, LoadSkipsCommentsBlanksAndMalformedLines) {
    const fs::path dir = fs::temp_directory_path() / "hestia_test_cfg_parse";
    const fs::path path = dir / "config";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream out(path);
        out << "# a comment\n"
            << "\n"
            << "no_equals_sign_here\n"
            << "good=value\n";
    }
    const Config cfg = Config::load(path);
    EXPECT_EQ(cfg.get("good").value_or(""), "value");
    EXPECT_FALSE(cfg.get("# a comment").has_value());
    EXPECT_FALSE(cfg.get("no_equals_sign_here").has_value());
    EXPECT_EQ(cfg.entries().size(), 1u);

    fs::remove_all(dir);
}

TEST(Config, DataHomePrecedence) {
    const char *saved_home = std::getenv("HESTIA_HOME");
    const fs::path override_dir = fs::temp_directory_path() / "hestia_override";
    const fs::path env_dir = fs::temp_directory_path() / "hestia_env";

    set_env("HESTIA_HOME", env_dir.string().c_str());
    // An explicit override beats the environment.
    EXPECT_EQ(hestia::config::data_home(override_dir), override_dir);
    // With no override, HESTIA_HOME wins over the persisted pointer / default.
    EXPECT_EQ(hestia::config::data_home(), env_dir);
    // config_path hangs the file off the resolved data dir.
    EXPECT_EQ(hestia::config::config_path(override_dir), override_dir / "config");

    set_env("HESTIA_HOME", saved_home);
}
