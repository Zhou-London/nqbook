#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <nq/adapter_abi.h>

namespace nq::mbo_file {

inline constexpr char kMagic[8] = {'N', 'Q', 'M', 'B', 'O', '\0', '\0', '\0'};
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::size_t kSymbolSize = 28;

struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t n_instruments;
};

struct InstrumentRecord {
    std::uint32_t id;
    char symbol[kSymbolSize];
    std::int64_t tick_size;
    std::int64_t lot_size;
};

static_assert(sizeof(Header) == 16);
static_assert(sizeof(InstrumentRecord) == 48);
static_assert(sizeof(nq_mbo) == 40);

class Writer {
public:
    explicit Writer(const std::filesystem::path& path, const std::vector<InstrumentRecord>& instruments)
        : file_(std::fopen(path.string().c_str(), "wb")) {
        if (!file_) throw std::runtime_error("cannot create " + path.string());
        Header h{};
        std::memcpy(h.magic, kMagic, sizeof(kMagic));
        h.version = kVersion;
        h.n_instruments = static_cast<std::uint32_t>(instruments.size());
        Put(&h, sizeof(h));
        for (const auto& i : instruments) Put(&i, sizeof(i));
    }

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    ~Writer() {
        if (file_) std::fclose(file_);
    }

    void Write(const nq_mbo& m) { Put(&m, sizeof(m)); }

    void Close() {
        if (file_ && std::fclose(file_) != 0) throw std::runtime_error("close failed");
        file_ = nullptr;
    }

    static InstrumentRecord MakeInstrument(std::uint32_t id, const std::string& symbol, std::int64_t tick_size,
                                           std::int64_t lot_size) {
        InstrumentRecord r{};
        r.id = id;
        std::strncpy(r.symbol, symbol.c_str(), kSymbolSize - 1);
        r.tick_size = tick_size;
        r.lot_size = lot_size;
        return r;
    }

private:
    void Put(const void* p, std::size_t n) {
        if (std::fwrite(p, 1, n, file_) != n) throw std::runtime_error("write failed");
    }

    std::FILE* file_;
};

class Reader {
public:
    explicit Reader(const std::filesystem::path& path) : file_(std::fopen(path.string().c_str(), "rb")) {
        if (!file_) throw std::runtime_error("cannot open " + path.string());
        Header h{};
        if (std::fread(&h, 1, sizeof(h), file_) != sizeof(h) || std::memcmp(h.magic, kMagic, sizeof(kMagic)) != 0) {
            std::fclose(file_);
            throw std::runtime_error("not an mbo file: " + path.string());
        }
        if (h.version != kVersion) {
            std::fclose(file_);
            throw std::runtime_error("unsupported mbo file version " + std::to_string(h.version) + ": " + path.string());
        }
        instruments_.resize(h.n_instruments);
        for (auto& i : instruments_) {
            if (std::fread(&i, 1, sizeof(i), file_) != sizeof(i)) {
                std::fclose(file_);
                throw std::runtime_error("truncated instrument table: " + path.string());
            }
            i.symbol[kSymbolSize - 1] = '\0';
        }
    }

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    ~Reader() {
        if (file_) std::fclose(file_);
    }

    const std::vector<InstrumentRecord>& Instruments() const noexcept { return instruments_; }

    int Next(nq_mbo& out) noexcept {
        const std::size_t n = std::fread(&out, 1, sizeof(out), file_);
        if (n == sizeof(out)) return 1;
        if (n == 0 && std::feof(file_)) return 0;
        return -1;
    }

private:
    std::FILE* file_;
    std::vector<InstrumentRecord> instruments_;
};

}  // namespace nq::mbo_file
