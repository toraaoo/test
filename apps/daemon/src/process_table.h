#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "process_types.h"

// The persisted process table: the in-memory records plus their on-disk backing.
// Persistence is atomic (write a temp file, then rename over the target) so a
// crash mid-write can never leave a half-written, unparseable table — the failure
// mode the old truncate-then-write had. A corrupt or absent file loads as empty.
// The coordinator (ProcessSupervisor) serializes access; this type is not itself
// thread-safe. See P3/A6 of the refactor.
namespace hestia::daemon {
    class ProcessTable {
    public:
        // Load the table from `path` (empty if missing or corrupt).
        explicit ProcessTable(std::filesystem::path path);

        // Direct access to the records for the coordinator's tick, which mutates
        // them in place under its own lock. Call save() to persist changes.
        std::map<std::string, ProcessRecord> &entries() { return records_; }
        const std::map<std::string, ProcessRecord> &entries() const { return records_; }

        std::optional<ProcessRecord> get(const std::string &id) const;
        std::vector<ProcessRecord> snapshot() const;

        // Atomically write the table to disk. Returns false on an I/O failure.
        bool save() const;

    private:
        void load();

        std::filesystem::path path_;
        std::map<std::string, ProcessRecord> records_;
    };
}
