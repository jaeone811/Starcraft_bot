#pragma once
// Locating files when BWAPI's working directory is not what we expect.

#include <filesystem>

namespace bb
{
  // Folder that contains this AI module (e.g. <StarCraft>/bwapi-data/AI), or empty if unknown.
  std::filesystem::path moduleDirectory();

  // StarCraft folder: the working directory if it has bwapi-data/, otherwise two levels
  // above the module when it sits in .../bwapi-data/<folder>/, otherwise the working directory.
  std::filesystem::path starcraftRoot();
}
