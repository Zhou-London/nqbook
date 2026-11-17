#include <Feature/FeatureCatalog.h>

#include <filesystem>
#include <memory>
#include <stdexcept>

#include <nq/params.hpp>

namespace nq {

namespace {

class PluginCross : public CrossOperator {
public:
    explicit PluginCross(host::CrossInstance cross) noexcept : cross_(std::move(cross)) {}

    void Apply(const CrossView& view) override { cross_.Apply(*view.Raw()); }

private:
    host::CrossInstance cross_;
};

void RequireTriggerFitsFeed(const SamplerSpec& spec, Feed feed) {
    if (spec.trigger == Trigger::Trades && feed != Feed::MBO) {
        throw FeedMismatchError("trades sampler needs an MBO feed: " + spec.name);
    }
}

}  // namespace

std::size_t FeatureCatalog::LoadPlugin(const std::string& path, std::string name, KeyValues params) {
    if (!samplers_.empty()) throw std::logic_error("LoadPlugin after AddSampler");
    if (!crosses_.empty()) throw std::logic_error("LoadPlugin after AddCross");
    if (name.empty()) name = std::filesystem::path(path).stem().string();
    for (const auto& existing : names_) {
        if (existing == name) throw std::invalid_argument("duplicate plugin name: " + name);
    }

    host::Plugin plugin(path);
    const Feed feed = static_cast<Feed>(plugin.Schema().feed);
    if (!plugins_.empty() && feed != feed_) {
        throw FeedMismatchError("plugin " + name + " reads " + FeedName(feed) + ", earlier plugins read " +
                                FeedName(feed_));
    }
    feed_ = feed;
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
    if (FindSampler(spec.name) || FindCross(spec.name)) {
        throw std::invalid_argument("duplicate sampler name: " + spec.name);
    }
    RequireTriggerFitsFeed(spec, feed_);

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

void FeatureCatalog::AddCross(CrossSpec spec) {
    if (spec.every <= 0) throw std::invalid_argument("cross period must be positive: " + spec.name);
    if (FindSampler(spec.name) || FindCross(spec.name)) {
        throw std::invalid_argument("duplicate sampler name: " + spec.name);
    }
    if (!spec.make) throw std::invalid_argument("cross has no operator: " + spec.name);
    if (spec.inputs.empty()) throw std::invalid_argument("cross has no input columns: " + spec.name);

    CrossTemplate t;
    for (const auto& n : spec.inputs) {
        const std::size_t i = FindColumn(n);
        if (columns_[i].kind != NQ_COLUMN_SNAPSHOT) {
            throw std::invalid_argument("cross input must be a snapshot column: " + n);
        }
        t.inputs.push_back(i);
        t.names.push_back(columns_[i].name);
    }
    for (const auto& n : spec.outputs) t.names.push_back(spec.name + "." + n);
    t.spec = std::move(spec);
    crosses_.push_back(std::move(t));
}

std::size_t FeatureCatalog::FindPlugin(std::string_view name) const {
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) return i;
    }
    throw std::invalid_argument("unknown plugin: " + std::string(name));
}

void FeatureCatalog::AddCross(std::string name, std::string_view plugin, std::int64_t every, bool fill_gaps) {
    const std::size_t index = FindPlugin(plugin);
    const host::Plugin* p = &plugins_[index];
    if (!p->HasCross()) throw std::invalid_argument("plugin has no cross: " + names_[index]);
    for (const auto& c : crosses_) {
        if (c.plugin == index) throw std::invalid_argument("plugin already has a cross: " + names_[index]);
    }

    CrossSpec spec;
    spec.name = std::move(name);
    spec.every = every;
    spec.fill_gaps = fill_gaps;
    for (const char* input : p->CrossInputs()) spec.inputs.push_back(names_[index] + "." + (input ? input : ""));
    for (const nq_column& c : p->CrossColumns()) spec.outputs.emplace_back(c.name ? c.name : "");
    spec.make = [p, params = params_[index]]() -> std::unique_ptr<CrossOperator> {
        ParamsBuilder pb;
        pb.PriceScale(NQ_PRICE_SCALE).QtyScale(NQ_QTY_SCALE);
        for (const auto& [k, v] : params) pb.Set(k, v);
        return std::make_unique<PluginCross>(p->CreateCross(*pb.Build()));
    };
    AddCross(std::move(spec));
    crosses_.back().plugin = index;
}

void FeatureCatalog::RequireFeed(Feed adapter_feed) const {
    if (!plugins_.empty() && feed_ != adapter_feed) {
        throw FeedMismatchError(std::string("plugins read ") + FeedName(feed_) + ", the adapter sends " +
                                FeedName(adapter_feed));
    }
    for (const auto& s : samplers_) RequireTriggerFitsFeed(s.spec, adapter_feed);
}

const SamplerTemplate* FeatureCatalog::FindSampler(std::string_view name) const noexcept {
    for (const auto& s : samplers_) {
        if (s.spec.name == name) return &s;
    }
    return nullptr;
}

const CrossTemplate* FeatureCatalog::FindCross(std::string_view name) const noexcept {
    for (const auto& c : crosses_) {
        if (c.spec.name == name) return &c;
    }
    return nullptr;
}

}  // namespace nq
