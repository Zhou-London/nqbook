#pragma once

#include <cstdio>

#include <Feature/FeatureCatalog.h>
#include <PipelineTable.h>
#include <Session.h>

namespace nq {

void PrintCatalogText(std::FILE* out, const FeatureCatalog& catalog);
void PrintCatalogJson(std::FILE* out, const FeatureCatalog& catalog);

void PrintFeedReport(std::FILE* out, const PipelineTable<nq_mbo>& table, const RunResult& result);
void PrintFeedReport(std::FILE* out, const PipelineTable<nq_ohlcv>& table, const RunResult& result);

}  // namespace nq
