#include <Config.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <stdexcept>

namespace nq {

namespace {

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

}  // namespace

std::int64_t ParseDuration(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
    if (i == 0) throw std::invalid_argument("duration must start with digits: '" + std::string(text) + "'");
    std::int64_t value = 0;
    std::from_chars(text.data(), text.data() + i, value);
    const std::string_view unit = text.substr(i);
    std::int64_t scale = 0;
    if (unit == "ns") scale = 1;
    else if (unit == "us") scale = 1'000;
    else if (unit == "ms") scale = 1'000'000;
    else if (unit == "s") scale = 1'000'000'000;
    else if (unit == "m") scale = 60'000'000'000;
    else if (unit == "h") scale = 3'600'000'000'000;
    else throw std::invalid_argument("duration needs a unit (ns, us, ms, s, m, h): '" + std::string(text) + "'");
    if (value <= 0) throw std::invalid_argument("duration must be positive: '" + std::string(text) + "'");
    return value * scale;
}

SamplerSpec ParseSamplerSpec(std::string_view text) {
    const auto eq = text.find('=');
    if (eq == std::string_view::npos || eq == 0) {
        throw std::invalid_argument("sampler must be NAME=TRIGGER:EVERY: '" + std::string(text) + "'");
    }
    const std::string name(text.substr(0, eq));
    const std::string_view rest = text.substr(eq + 1);
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == rest.size()) {
        throw std::invalid_argument("sampler must be NAME=TRIGGER:EVERY: '" + std::string(text) + "'");
    }
    const std::string_view trigger = rest.substr(0, colon);
    const std::string_view every = rest.substr(colon + 1);
    if (trigger == "time") return SamplerSpec::Time(name, ParseDuration(every));
    if (trigger == "events") return SamplerSpec::Events(name, ParseCount(every, "events count"));
    if (trigger == "trades") return SamplerSpec::Trades(name, ParseCount(every, "trades count"));
    throw std::invalid_argument("sampler trigger must be time, events, or trades: '" + std::string(trigger) + "'");
}

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
        const auto eq = t.find('=');
        if (eq == std::string_view::npos || eq == 0) {
            throw std::invalid_argument(path.string() + ":" + std::to_string(lineno) + ": expected key=value");
        }
        out.emplace_back(std::string(Trim(t.substr(0, eq))), std::string(Trim(t.substr(eq + 1))));
    }
    return out;
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

}  // namespace nq
