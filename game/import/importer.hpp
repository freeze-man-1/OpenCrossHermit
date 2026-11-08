// ============================================================================
// importer.hpp — installs the player's own game files into the data folder
// ============================================================================
// Inputs (any order, several at once):
//   the JP disc        EB2002CH.iso, or its data.z
//   the TW disc        the disc image, or its cd1
//   the JP 1.2 update  "Cross Hermit Update 1.2.exe" (required: its exe is
//                      the JP version's, and the source of the TW build's
//                      data too), or that "Cross Hermit.exe" alone for a
//                      TW-only install
// Layout of the data folder (vfs::dataRoot):
//   jp/data/..., tw/data/...   each release's files that differ
//   common/data/...            the files both releases have the same
//   common/static/jp.exe       the JP 1.2 exe; static/tw.exe the TW disc's
//   common/static/ch_data.bin  rebuilt by resources/ch_data.chpatch
//   installed.txt              the releases fully imported, one per line
// The work is cut into steps so the web build can import without threads.
#pragma once
#include "archives.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ch::import {

enum class Kind { Unknown, JpDisc, TwDisc, JpUpdate, JpExe, TwExe };

struct Input {
    std::string path;
    Kind kind = Kind::Unknown;
    std::string note;   // what was found, or why it was not recognised
};

// Looks at a file's contents (not its name).
Input identify(const std::string& path);
[[nodiscard]] const char* kindName(Kind k);

struct Installed {
    bool jp = false, tw = false;       // fully imported and playable
    bool jpExe = false;                // the JP 1.2 exe is in place
};
Installed installed();

class Importer {
public:
    Importer();
    ~Importer();
    // Adds a file; returns what it is. A file of a kind already added replaces it.
    const Input& add(const std::string& path);
    [[nodiscard]] const std::vector<Input>& inputs() const { return inputs_; }
    // Whether the inputs (with what is installed already) make a playable
    // release; otherwise what is missing.
    [[nodiscard]] bool ready(std::string* missing = nullptr) const;
    void start();
    // Runs for about `budgetMs`; true while there is more to do.
    bool step(double budgetMs);
    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] double progress() const;               // 0..1
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] const std::string& error() const { return error_; }

private:
    struct Job {
        std::string label;
        std::uint64_t weight = 1;
        std::function<bool()> run;   // true when done
    };
    void plan();
    std::vector<Input> inputs_;
    std::vector<Job> jobs_;
    std::size_t next_ = 0;
    std::uint64_t doneWeight_ = 0, totalWeight_ = 0;
    double partial_ = 0;             // the running job's own progress, 0..1
    bool running_ = false, finished_ = false;
    std::string status_, error_;
    struct State;
    std::unique_ptr<State> st_;
};

// Runs a whole import in the console: `game --import FILE...`. Returns the exit code.
int importCli(const std::vector<std::string>& paths);

} // namespace ch::import
