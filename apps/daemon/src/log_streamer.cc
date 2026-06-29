#include "log_streamer.h"

#include <fstream>
#include <iterator>
#include <vector>

namespace hestia::daemon {
    namespace fs = std::filesystem;

    namespace {
        std::uint64_t current_size(const fs::path &path) {
            std::error_code ec;
            const auto size = fs::file_size(path, ec);
            return ec ? 0 : static_cast<std::uint64_t>(size);
        }
    }

    void LogStreamer::reset(const std::string &id, const fs::path &path) {
        offsets_[id] = current_size(path);
    }

    void LogStreamer::forget(const std::string &id) {
        offsets_.erase(id);
    }

    std::string LogStreamer::read_new(const std::string &id, const fs::path &path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return {};
        f.seekg(0, std::ios::end);
        const std::streamoff size = f.tellg();
        if (size < 0) return {};
        auto &offset = offsets_[id];
        if (static_cast<std::uint64_t>(size) < offset) offset = 0; // truncated/rotated
        if (static_cast<std::uint64_t>(size) == offset) return {}; // nothing new
        f.seekg(static_cast<std::streamoff>(offset));
        std::string chunk((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        offset += chunk.size();
        return chunk;
    }

    std::string LogStreamer::tail(const fs::path &path, int max_lines) {
        std::ifstream f(path);
        if (!f) return {};
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line)) lines.push_back(line);
        const std::size_t keep = max_lines > 0 ? static_cast<std::size_t>(max_lines) : 0;
        const std::size_t begin = lines.size() > keep ? lines.size() - keep : 0;
        std::string out;
        for (std::size_t i = begin; i < lines.size(); ++i) {
            out += lines[i];
            out += '\n';
        }
        return out;
    }
}
