#include <Feature/FeatureCatalog.h>

#include <filesystem>
#include <stdexcept>

namespace nq {

namespace {

void RequireTriggerFitsFeed(const SamplerSpec& spec, Feed feed) {
    if (spec.trigger == Trigger::Trades && feed != Feed::MBO) {
        throw FeedMismatchError("trades sampler needs an MBO feed: " + spec.name);
    }
}

}  // namespace

std::size_t FeatureCatalog::LoadPlugin(const std::string& path, std::string name, KeyValues params) {
    if (!samplers_.empty()) throw std::logic_error("LoadPlugin after AddSampler");
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
    if (FindSampler(spec.name)) throw std::invalid_argument("duplicate sampler name: " + spec.name);
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

}  // namespace nq
