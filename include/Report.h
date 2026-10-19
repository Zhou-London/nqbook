#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <Feature/FeatureCatalog.h>
#include <PipelineTable.h>
#include <Session.h>

namespace nq {

void PrintCatalogText(std::FILE* out, const FeatureCatalog& plugins);
void PrintCatalogJson(std::FILE* out, const FeatureCatalog& plugins);

void PrintFeedReport(std::FILE* out, const PipelineTable& table, const RunResult& result);

std::vector<std::filesystem::path> WriteSamplerCsv(const PipelineTable& table, const std::filesystem::path& dir);

}  // namespace nq
