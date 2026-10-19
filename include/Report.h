#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <Feature/PluginSet.h>
#include <Market.h>
#include <Runner.h>

namespace nq {

void PrintPluginSetText(std::FILE* out, const PluginSet& plugins);
void PrintPluginSetJson(std::FILE* out, const PluginSet& plugins);

void PrintFeedReport(std::FILE* out, const Market& market, const RunResult& result);

std::vector<std::filesystem::path> WriteSamplerCsv(const Market& market, const std::filesystem::path& dir);

}  // namespace nq
