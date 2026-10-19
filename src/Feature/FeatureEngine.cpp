#include <Feature/FeatureEngine.h>

#include <filesystem>
#include <stdexcept>

#include <nq/params.hpp>

namespace nq {

static_assert(static_cast<std::uint8_t>(nlib::side::bid) == NQ_BID);
static_assert(static_cast<std::uint8_t>(nlib::side::ask) == NQ_ASK);
static_assert(static_cast<std::uint8_t>(nlib::action::add) == NQ_ADD);
static_assert(static_cast<std::uint8_t>(nlib::action::cancel) == NQ_CANCEL);
static_assert(static_cast<std::uint8_t>(nlib::action::modify) == NQ_MODIFY);
static_assert(static_cast<std::uint8_t>(nlib::action::trade) == NQ_TRADE);
static_assert(static_cast<std::uint8_t>(nlib::action::clear) == NQ_CLEAR);

FeatureEngine::FeatureEngine(const Orderbook& book, InstrumentSpec spec)
    : adapter_(book), spec_(std::move(spec)) {}

std::size_t FeatureEngine::LoadPlugin(const std::string& path, std::string name, const KeyValues& kv) {
    if (!samplers_.empty()) throw std::logic_error("LoadPlugin after AddSampler");
    if (name.empty()) name = std::filesystem::path(path).stem().string();
    for (const auto& existing : plugin_names_) {
        if (existing == name) throw std::invalid_argument("duplicate plugin name: " + name);
    }

    host::Plugin plugin(path);

    ParamsBuilder pb;
    pb.Symbol(spec_.symbol)
        .TickSize(spec_.tick_size)
        .LotSize(spec_.lot_size)
        .PriceScale(nlib::price_scale)
        .QtyScale(nlib::qty_scale);
    for (const auto& [k, v] : kv) pb.Set(k, v);
    host::PluginInstance instance = plugin.Create(*pb.Build());

    const std::size_t index = plugins_.size();
    const std::size_t offset = values_.size();
    for (std::size_t i = 0; i < plugin.Schema().ncol; ++i) {
        const nq_column& c = plugin.Columns()[i];
        columns_.push_back(ColumnInfo{name + "." + (c.name ? c.name : ""), c.unit ? c.unit : "",
                                      c.doc ? c.doc : "", c.kind, index, i});
    }
    values_.resize(values_.size() + plugin.Schema().ncol, 0.0);
    offsets_.push_back(offset);
    plugin_names_.push_back(std::move(name));
    plugins_.push_back(std::move(plugin));
    instances_.push_back(std::move(instance));
    values_fresh_ = false;
    return index;
}

std::size_t FeatureEngine::FindColumn(std::string_view name) const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].name == name) return i;
    }
    throw std::invalid_argument("unknown column: " + std::string(name));
}

Sampler& FeatureEngine::AddSampler(SamplerSpec spec) {
    if (spec.every <= 0) throw std::invalid_argument("sampler period must be positive: " + spec.name);
    if (FindSampler(spec.name)) throw std::invalid_argument("duplicate sampler name: " + spec.name);

    std::vector<std::string> names;
    std::vector<SamplerColumn> cols;
    if (spec.columns.empty()) {
        names.reserve(columns_.size());
        cols.reserve(columns_.size());
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            names.push_back(columns_[i].name);
            cols.push_back(SamplerColumn{i, columns_[i].kind});
        }
    } else {
        names.reserve(spec.columns.size());
        cols.reserve(spec.columns.size());
        for (const auto& n : spec.columns) {
            const std::size_t i = FindColumn(n);
            names.push_back(columns_[i].name);
            cols.push_back(SamplerColumn{i, columns_[i].kind});
        }
    }
    samplers_.push_back(std::make_unique<Sampler>(std::move(spec), std::move(names), std::move(cols)));
    return *samplers_.back();
}

Sampler* FeatureEngine::FindSampler(std::string_view name) noexcept {
    for (auto& s : samplers_) {
        if (s->Spec().name == name) return s.get();
    }
    return nullptr;
}

std::span<const double> FeatureEngine::ReadAll() {
    if (!values_fresh_) {
        std::span<double> all(values_);
        for (std::size_t i = 0; i < instances_.size(); ++i) {
            instances_[i].Read(all.subspan(offsets_[i], instances_[i].Ncol()), adapter_.View());
        }
        values_fresh_ = true;
    }
    return values_;
}

void FeatureEngine::Advance(std::int64_t ts) {
    for (auto& s : samplers_) s->Advance(ts, [this] { return ReadAll(); });
}

void FeatureEngine::OnEvent(const nlib::mbo& m, nlib::qty_t filled) {
    values_fresh_ = false;
    if (m.action == nlib::action::clear) {
        const nq_control reset{NQ_CONTROL_RESET, m.ts};
        for (auto& inst : instances_) inst.OnControl(reset);
        for (auto& s : samplers_) s->ResetBaseline();
    } else {
        const nq_event e{m.ts, m.id, m.price, m.qty, filled,
                         static_cast<std::uint8_t>(m.side), static_cast<std::uint8_t>(m.action)};
        for (auto& inst : instances_) inst.OnEvent(e, adapter_.View());
    }
    for (auto& s : samplers_) {
        if (s->OnEvent(m.action)) s->Emit(m.ts, ReadAll());
    }
}

}  // namespace nq
