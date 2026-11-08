// ============================================================================
// archives.hpp — the containers the game ships in
// ============================================================================
//   ISO 9660 disc images (both releases)
//   data.z      the JP disc's InstallShield 3 archive, PKWare DCL ("blast")
//   cd1         the TW disc's "T-TIME" installer archive, zlib records
//   zip         the JP 1.2 update (a self-extracting zip, stored / deflated)
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ch::import {

// Random access to bytes: a file, or a slice of another source.
class Source {
public:
    virtual ~Source() = default;
    [[nodiscard]] virtual std::uint64_t size() const = 0;
    virtual void read(std::uint64_t offset, void* dst, std::size_t n) const = 0;
    [[nodiscard]] std::vector<std::uint8_t> bytes(std::uint64_t offset, std::size_t n) const {
        std::vector<std::uint8_t> v(n);
        if (n) read(offset, v.data(), n);
        return v;
    }
};

class FileSource final : public Source {
public:
    explicit FileSource(const std::string& path);
    ~FileSource() override;
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;
    [[nodiscard]] std::uint64_t size() const override { return size_; }
    void read(std::uint64_t offset, void* dst, std::size_t n) const override;

private:
    std::FILE* f_ = nullptr;
    std::uint64_t size_ = 0;
};

class SliceSource final : public Source {
public:
    SliceSource(std::shared_ptr<const Source> base, std::uint64_t offset, std::uint64_t size)
        : base_(std::move(base)), offset_(offset), size_(size) {}
    [[nodiscard]] std::uint64_t size() const override { return size_; }
    void read(std::uint64_t offset, void* dst, std::size_t n) const override;

private:
    std::shared_ptr<const Source> base_;
    std::uint64_t offset_, size_;
};

// A file inside an archive. `path` uses '/' separators, as stored (case kept).
struct Entry {
    std::string path;
    std::uint64_t offset = 0, size = 0, packed = 0;
    int method = 0;   // zip: 0 stored, 8 deflate
};

// ---- ISO 9660 ----
[[nodiscard]] bool isIso(const Source& s);
// Every file on the disc ("DATA.Z", "DirectX/DX.exe"); versions (";1") dropped.
std::vector<Entry> isoFiles(const Source& s);

// ---- InstallShield 3 (data.z) ----
[[nodiscard]] bool isIs3(const Source& s);
std::vector<Entry> is3Files(const Source& s);
std::vector<std::uint8_t> is3Extract(const Source& s, const Entry& e);
// PKWare Data Compression Library "implode" streams (zlib's contrib/blast).
std::vector<std::uint8_t> blast(std::span<const std::uint8_t> in, std::size_t sizeHint = 0);

// ---- T-TIME (cd1) ----
[[nodiscard]] bool isTTime(const Source& s);
// The files in order, one per next(): false at the end. With want(path)
// false the file's data is skipped (read past, not inflated).
class TTimeReader {
public:
    explicit TTimeReader(const Source& s) : s_(s) {}
    bool next(std::string& path, std::vector<std::uint8_t>& data, const std::function<bool(const std::string&)>& want);
    [[nodiscard]] std::uint64_t position() const { return at_; }

private:
    const Source& s_;
    std::uint64_t at_ = 0xa8;
    std::string dir_;
};

// ---- zip ----
[[nodiscard]] bool isZip(const Source& s);
std::vector<Entry> zipFiles(const Source& s);
std::vector<std::uint8_t> zipExtract(const Source& s, const Entry& e);

} // namespace ch::import
