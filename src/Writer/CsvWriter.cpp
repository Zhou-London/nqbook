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

std::vector<std::filesystem::path> WriteSamplerCsv(const PipelineTable& table, const std::filesystem::path& dir) {
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

        for (const auto& [id, e] : table.Entries()) {
            const std::string symbol = CsvString(e.pipeline->Engine().GetInstrument().symbol);
            const RowBuffer& rows = e.pipeline->Engine().GetSampler(si).Rows();
            for (std::size_t r = 0; r < rows.RowCount(); ++r) {
                std::fprintf(f.get(), "%u,%s,%lld", id, symbol.c_str(), static_cast<long long>(rows.Ts(r)));
                for (const double v : rows.Row(r)) std::fprintf(f.get(), ",%.17g", v);
                std::fprintf(f.get(), "\n");
            }
        }
        written.push_back(path);
    }
    return written;
}

}  // namespace nq
