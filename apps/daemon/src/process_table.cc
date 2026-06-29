#include "process_table.h"

#include <fstream>

#include <hestia/ipc/process_codec.h>

namespace hestia::daemon {
    namespace fs = std::filesystem;
    using nlohmann::json;

    ProcessTable::ProcessTable(fs::path path) : path_(std::move(path)) {
        load();
    }

    std::optional<ProcessRecord> ProcessTable::get(const std::string &id) const {
        const auto it = records_.find(id);
        if (it == records_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<ProcessRecord> ProcessTable::snapshot() const {
        std::vector<ProcessRecord> out;
        out.reserve(records_.size());
        for (const auto &[id, rec]: records_) out.push_back(rec);
        return out;
    }

    bool ProcessTable::save() const {
        std::error_code ec;
        fs::create_directories(path_.parent_path(), ec);

        json j = json::array();
        for (const auto &[id, rec]: records_) j.push_back(ipc::to_json(rec));

        // Atomic replace: write a sibling temp file, flush it, then rename over
        // the target. A crash leaves either the old table or the new one intact,
        // never a truncated one.
        const fs::path tmp = path_.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f) return false;
            f << j.dump(2);
            f.flush();
            if (!f) return false;
        }
        fs::rename(tmp, path_, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
        return true;
    }

    void ProcessTable::load() {
        std::ifstream f(path_);
        if (!f) return;
        try {
            json j;
            f >> j;
            for (const auto &entry: j) {
                auto rec = ipc::record_from_json(entry);
                records_[rec.id] = rec;
            }
        } catch (...) {
            // A corrupt table is not fatal: start with an empty one.
            records_.clear();
        }
    }
}
