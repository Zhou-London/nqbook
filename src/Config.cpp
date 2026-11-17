#include <Config.h>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>

namespace nq {

namespace {

/* ---------- Parsing Helpers ---------- */
std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::int64_t ParseCount(std::string_view text, std::string_view what) {
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value <= 0) {
        throw std::invalid_argument(std::string(what) + " must be a positive integer: '" + std::string(text) + "'");
    }
    return value;
}

struct KeyValueView {
    std::string_view key;
    std::string_view value;
};

std::optional<KeyValueView> SplitKeyValue(std::string_view text) {
    const auto eq = text.find('=');
    if (eq == std::string_view::npos || eq == 0) return std::nullopt;
    return KeyValueView{text.substr(0, eq), text.substr(eq + 1)};
}

struct DurationUnit {
    std::string_view suffix;
    std::int64_t nanoseconds;
};

constexpr DurationUnit kDurationUnits[] = {
    {"ns", 1}, {"us", 1'000}, {"ms", 1'000'000}, {"s", 1'000'000'000}, {"m", 60'000'000'000}, {"h", 3'600'000'000'000},
};

KeyValues LoadParamsFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) throw std::invalid_argument("cannot open params file: " + path.string());
    
    KeyValues out;
    std::string line;
    std::size_t lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        const std::string_view t = Trim(line);
        if (t.empty() || t.front() == '#') continue;
        
        const auto kv = SplitKeyValue(t);
        if (!kv) throw std::invalid_argument(path.string() + ":" + std::to_string(lineno) + ": expected key=value");
        
        out.emplace_back(std::string(Trim(kv->key)), std::string(Trim(kv->value)));
    }
    return out;
}

}  // namespace

std::int64_t ParseDuration(std::string_view text) {
    const char* const last = text.data() + text.size();
    std::int64_t value = 0;
    const auto [digits_end, ec] = std::from_chars(text.data(), last, value);
    if (ec == std::errc::invalid_argument || (ec == std::errc{} && value <= 0)) {
        throw std::invalid_argument("duration must start with a positive integer: '" + std::string(text) + "'");
    }

    const auto unit = std::ranges::find(kDurationUnits, std::string_view(digits_end, last), &DurationUnit::suffix);
    if (unit == std::end(kDurationUnits)) {
        throw std::invalid_argument("duration needs a unit (ns, us, ms, s, m, h): '" + std::string(text) + "'");
    }
    if (ec == std::errc::result_out_of_range || value > std::numeric_limits<std::int64_t>::max() / unit->nanoseconds) {
        throw std::invalid_argument("duration is too large: '" + std::string(text) + "'");
    }
    return value * unit->nanoseconds;
}

SamplerSpec ParseSamplerSpec(std::string_view text, bool fill_gaps) {
    const auto named = SplitKeyValue(text);
    const auto colon = named ? named->value.find(':') : std::string_view::npos;
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == named->value.size()) {
        throw std::invalid_argument("sampler must be NAME=TRIGGER:EVERY: '" + std::string(text) + "'");
    }
    const std::string name(named->key);
    const std::string_view trigger = named->value.substr(0, colon);
    const std::string_view every = named->value.substr(colon + 1);
    if (trigger == "time") return SamplerSpec::Time(name, ParseDuration(every), fill_gaps);
    if (trigger == "events") return SamplerSpec::Events(name, ParseCount(every, "events count"));
    if (trigger == "trades") return SamplerSpec::Trades(name, ParseCount(every, "trades count"));
    throw std::invalid_argument("sampler trigger must be time, events, or trades: '" + std::string(trigger) + "'");
}

CrossArg ParseCrossArg(std::string_view text) {
    const auto named = SplitKeyValue(text);
    const auto colon = named ? named->value.rfind(':') : std::string_view::npos;
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == named->value.size()) {
        throw std::invalid_argument("cross must be NAME=PLUGIN:EVERY: '" + std::string(text) + "'");
    }
    return CrossArg{std::string(named->key), std::string(named->value.substr(0, colon)),
                    ParseDuration(named->value.substr(colon + 1))};
}

PluginFile DescribePluginFile(const std::filesystem::path& so) {
    PluginFile pf{so, so.stem().string(), {}};
    std::filesystem::path params = so;
    
    params.replace_extension(".params");
    if (std::filesystem::is_regular_file(params)) pf.params = LoadParamsFile(params);
    
    return pf;
}

std::vector<PluginFile> ScanPluginDir(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".so") files.push_back(entry.path());
    }
    
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.filename() < b.filename(); });
    
    std::vector<PluginFile> out;
    out.reserve(files.size());
    for (const auto& f : files) out.push_back(DescribePluginFile(f));
    
    return out;
}

KeyValues AdapterParams(const std::filesystem::path& adapter, std::span<const std::string> overrides) {
    KeyValues out = DescribePluginFile(adapter).params;
    for (const std::string& text : overrides) {
        const auto kv = SplitKeyValue(text);
        if (!kv) throw std::invalid_argument("--adapter-param expects KEY=VALUE: '" + text + "'");
        out.emplace_back(kv->key, kv->value);
    }
    return out;
}

}  // namespace nq
