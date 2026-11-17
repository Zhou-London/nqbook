#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Feature/Sampler.h>
#include <KeyValues.h>

namespace nq {

struct PluginFile {
    std::filesystem::path path;
    std::string name;
    KeyValues params;
};

struct CrossArg {
    std::string name;
    std::string plugin;
    std::int64_t every = 0;
};

/* ---------- Parsing Functions ---------- */
std::int64_t ParseDuration(std::string_view text);
SamplerSpec ParseSamplerSpec(std::string_view text, bool fill_gaps = false);
CrossArg ParseCrossArg(std::string_view text);

PluginFile DescribePluginFile(const std::filesystem::path& so);
std::vector<PluginFile> ScanPluginDir(const std::filesystem::path& dir);
KeyValues AdapterParams(const std::filesystem::path& adapter, std::span<const std::string> overrides);

}  // namespace nq
