#pragma once

#include <filesystem>
#include <vector>

#include <PipelineTable.h>

namespace nq {

std::vector<std::filesystem::path> WriteSamplerCsv(const PipelineTable& table, const std::filesystem::path& dir);

}  // namespace nq
