#pragma once

#include <filesystem>
#include <vector>

#include <PipelineTable.h>

namespace nq {

template <class Record>
std::vector<std::filesystem::path> WriteSamplerCsv(const PipelineTable<Record>& table,
                                                   const std::filesystem::path& dir);

}  // namespace nq
