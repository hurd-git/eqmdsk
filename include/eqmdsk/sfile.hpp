#pragma once

#include <string>

#include "eqmdsk/file.hpp"

namespace eqmdsk {

// Reader and writer for the four-column EFIT S-file format.
//
// Up to three leading non-data records are exposed as XLABEL, YLABEL, and
// TITLE. The writer emits only these labels and the four standard data columns.
class SFile final : public FieldFile {
 public:
  explicit SFile(std::string filename);

  using FieldFile::write;
  const char* format_name() const noexcept override { return "SFile"; }
  void write(const std::string& path) const override;

 private:
  void parse(const std::string& bytes);
  void validate_for_write() const;
};

}  // namespace eqmdsk
