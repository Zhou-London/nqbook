#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Feature/Sampler.h>

namespace nq {

using KeyValues = std::vector<std::pair<std::string, std::string>>;

struct PluginFile {
    std::filesystem::path path;
    std::string name;
    KeyValues params;
};

std::int64_t ParseDuration(std::string_view text);
SamplerSpec ParseSamplerSpec(std::string_view text);
KeyValues LoadParamsFile(const std::filesystem::path& path);
PluginFile DescribePluginFile(const std::filesystem::path& so);
std::vector<PluginFile> ScanPluginDir(const std::filesystem::path& dir);

}  // namespace nq
