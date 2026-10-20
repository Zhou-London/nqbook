#pragma once

#include <cstdio>

#include <Feature/FeatureCatalog.h>
#include <PipelineTable.h>
#include <Session.h>

namespace nq {

void PrintCatalogText(std::FILE* out, const FeatureCatalog& catalog);
void PrintCatalogJson(std::FILE* out, const FeatureCatalog& catalog);

void PrintFeedReport(std::FILE* out, const PipelineTable& table, const RunResult& result);

}  // namespace nq
