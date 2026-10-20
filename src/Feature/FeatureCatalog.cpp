#include <Feature/FeatureCatalog.h>

#include <filesystem>
#include <stdexcept>

namespace nq {

std::size_t FeatureCatalog::LoadPlugin(const std::string& path, std::string name, KeyValues params) {
    if (!samplers_.empty()) throw std::logic_error("LoadPlugin after AddSampler");
    if (name.empty()) name = std::filesystem::path(path).stem().string();
    for (const auto& existing : names_) {
        if (existing == name) throw std::invalid_argument("duplicate plugin name: " + name);
    }

    host::Plugin plugin(path);
    const std::size_t index = plugins_.size();
    for (std::size_t i = 0; i < plugin.Schema().ncol; ++i) {
        const nq_column& c = plugin.Columns()[i];
        columns_.push_back(ColumnInfo{name + "." + (c.name ? c.name : ""), c.unit ? c.unit : "",
                                      c.doc ? c.doc : "", c.kind, index, i});
    }
    plugins_.push_back(std::move(plugin));
    names_.push_back(std::move(name));
    params_.push_back(std::move(params));
    return index;
}

std::size_t FeatureCatalog::FindColumn(std::string_view name) const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].name == name) return i;
    }
    throw std::invalid_argument("unknown column: " + std::string(name));
}

void FeatureCatalog::AddSampler(SamplerSpec spec) {
    if (spec.every <= 0) throw std::invalid_argument("sampler period must be positive: " + spec.name);
    if (FindSampler(spec.name)) throw std::invalid_argument("duplicate sampler name: " + spec.name);

    SamplerTemplate t;
    if (spec.columns.empty()) {
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            t.names.push_back(columns_[i].name);
            t.columns.push_back(SamplerColumn{i, columns_[i].kind});
        }
    } else {
        for (const auto& n : spec.columns) {
            const std::size_t i = FindColumn(n);
            t.names.push_back(columns_[i].name);
            t.columns.push_back(SamplerColumn{i, columns_[i].kind});
        }
    }
    t.spec = std::move(spec);
    samplers_.push_back(std::move(t));
}

const SamplerTemplate* FeatureCatalog::FindSampler(std::string_view name) const noexcept {
    for (const auto& s : samplers_) {
        if (s.spec.name == name) return &s;
    }
    return nullptr;
}

}  // namespace nq
