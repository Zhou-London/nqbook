#include <Writer/CsvWriter.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace nq {

namespace {

std::string CsvString(std::string_view s) {
    if (s.find_first_of(",\"\n") == std::string_view::npos) return std::string(s);
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

}  // namespace

template <class Record>
std::vector<std::filesystem::path> WriteSamplerCsv(const PipelineTable<Record>& table,
                                                   const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    std::vector<std::filesystem::path> written;
    const auto& templates = table.Catalog().Samplers();
    for (std::size_t si = 0; si < templates.size(); ++si) {
        const SamplerTemplate& t = templates[si];
        const std::filesystem::path path = dir / (t.spec.name + ".csv");
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(std::fopen(path.string().c_str(), "w"), &std::fclose);
        if (!f) throw std::runtime_error("cannot create " + path.string());

        std::fprintf(f.get(), "instrument,symbol,ts");
        for (const auto& n : t.names) std::fprintf(f.get(), ",%s", CsvString(n).c_str());
        std::fprintf(f.get(), "\n");

        for (const auto& e : table.Entries()) {
            if (!e.pipeline) continue;
            const std::string symbol = CsvString(e.pipeline->Engine().GetInstrument().symbol);
            const RowBuffer& rows = e.pipeline->Engine().GetSampler(si).Rows();
            for (std::size_t r = 0; r < rows.RowCount(); ++r) {
                std::fprintf(f.get(), "%u,%s,%lld", e.id, symbol.c_str(), static_cast<long long>(rows.Ts(r)));
                for (const double v : rows.Row(r)) std::fprintf(f.get(), ",%.17g", v);
                std::fprintf(f.get(), "\n");
            }
        }
        written.push_back(path);
    }

    const auto& crosses = table.Catalog().Crosses();
    for (std::size_t ci = 0; ci < crosses.size(); ++ci) {
        const CrossTemplate& t = crosses[ci];
        const std::filesystem::path path = dir / (t.spec.name + ".csv");
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(std::fopen(path.string().c_str(), "w"), &std::fclose);
        if (!f) throw std::runtime_error("cannot create " + path.string());

        std::fprintf(f.get(), "instrument,symbol,ts");
        for (const auto& n : t.names) std::fprintf(f.get(), ",%s", CsvString(n).c_str());
        std::fprintf(f.get(), "\n");

        for (const FeatureBlock& block : table.Crosses()[ci].Blocks()) {
            const std::size_t nrow = block.instruments.size();
            for (std::size_t r = 0; r < nrow; ++r) {
                const std::uint32_t id = block.instruments[r];
                const std::string symbol = CsvString(table.Find(id)->Engine().GetInstrument().symbol);
                std::fprintf(f.get(), "%u,%s,%lld", id, symbol.c_str(), static_cast<long long>(block.ts));
                for (std::size_t c = 0; c < t.names.size(); ++c) std::fprintf(f.get(), ",%.17g", block.values[c * nrow + r]);
                std::fprintf(f.get(), "\n");
            }
        }
        written.push_back(path);
    }
    return written;
}

template std::vector<std::filesystem::path> WriteSamplerCsv(const PipelineTable<nq_mbo>&, const std::filesystem::path&);
template std::vector<std::filesystem::path> WriteSamplerCsv(const PipelineTable<nq_ohlcv>&,
                                                            const std::filesystem::path&);

}  // namespace nq
