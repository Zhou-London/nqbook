#include <Writer/CsvWriter.h>

#include <cinttypes>
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

class CsvTable {
public:
    CsvTable(const std::filesystem::path& path, const std::vector<std::string>& columns)
        : file_(std::fopen(path.string().c_str(), "w"), &std::fclose) {
        if (!file_) throw std::runtime_error("cannot create " + path.string());
        std::fprintf(file_.get(), "instrument,symbol,ts");
        for (const auto& c : columns) std::fprintf(file_.get(), ",%s", CsvString(c).c_str());
        EndRow();
    }

    void BeginRow(std::uint32_t instrument, const std::string& symbol, std::int64_t ts) {
        std::fprintf(file_.get(), "%u,%s,%" PRId64, instrument, symbol.c_str(), ts);
    }
    void Value(double v) { std::fprintf(file_.get(), ",%.17g", v); }
    void EndRow() { std::fputc('\n', file_.get()); }

private:
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> file_;
};

}  // namespace

template <class Record>
std::vector<std::filesystem::path> WriteCsv(const PipelineTable<Record>& table, const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    std::vector<std::filesystem::path> written;
    const auto& templates = table.Catalog().Samplers();
    for (std::size_t si = 0; si < templates.size(); ++si) {
        const SamplerTemplate& t = templates[si];
        const std::filesystem::path path = dir / (t.spec.name + ".csv");
        CsvTable csv(path, t.names);

        table.ForEachInstrument([&](const auto& e) {
            const Instrument& spec = e.pipeline->Engine().GetInstrument();
            const std::string symbol = CsvString(spec.symbol);
            const RowBuffer& rows = e.pipeline->Engine().GetSampler(si).Rows();
            for (std::size_t r = 0; r < rows.RowCount(); ++r) {
                csv.BeginRow(spec.id, symbol, rows.Ts(r));
                for (const double v : rows.Row(r)) csv.Value(v);
                csv.EndRow();
            }
        });
        written.push_back(path);
    }

    const auto& crosses = table.Catalog().Crosses();
    for (std::size_t ci = 0; ci < crosses.size(); ++ci) {
        const CrossTemplate& t = crosses[ci];
        const std::filesystem::path path = dir / (t.spec.name + ".csv");
        CsvTable csv(path, t.names);

        for (const FeatureBlock& block : table.Crosses()[ci].Blocks()) {
            const std::size_t nrow = block.instruments.size();
            for (std::size_t r = 0; r < nrow; ++r) {
                const std::uint32_t id = block.instruments[r];
                const std::string symbol = CsvString(table.Find(id)->Engine().GetInstrument().symbol);
                csv.BeginRow(id, symbol, block.ts);
                for (std::size_t c = 0; c < t.names.size(); ++c) csv.Value(block.values[c * nrow + r]);
                csv.EndRow();
            }
        }
        written.push_back(path);
    }
    return written;
}

template std::vector<std::filesystem::path> WriteCsv(const PipelineTable<nq_mbo>&, const std::filesystem::path&);
template std::vector<std::filesystem::path> WriteCsv(const PipelineTable<nq_ohlcv>&, const std::filesystem::path&);

}  // namespace nq
