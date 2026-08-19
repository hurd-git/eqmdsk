#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "eqmdsk/file.hpp"

namespace eqmdsk {

// Reader and writer for the four-column EFIT S-file format.
//
// Up to three leading non-data records are exposed as XLABEL, YLABEL, and
// TITLE. Other non-data records are retained verbatim and anchored to the
// number of data rows that preceded them, so parse/write/parse does not lose
// interstitial text.
class SFile final : public EFITFile {
 public:
  explicit SFile(const std::filesystem::path& filename);

  using EFITFile::write;
  const char* format_name() const noexcept override { return "SFile"; }
  void write(const std::filesystem::path& path) const override;

 private:
  struct PreservedLine {
    std::size_t data_index = 0;
    std::string bytes;
  };

  void parse(const std::string& bytes);
  void validate_for_write() const;

  std::vector<PreservedLine> preserved_lines_;
};

}  // namespace eqmdsk
