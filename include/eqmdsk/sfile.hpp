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
  explicit SFile(std::string path);
  static SFile create(std::size_t count);
  SFile copy() const { return *this; }

  using FieldFile::save;
  const std::vector<std::string>& required_fields() const noexcept override;
  const std::vector<std::string>& optional_fields() const noexcept override;
  FieldKind field_kind(const std::string& name) const noexcept override;
  const char* format_name() const noexcept override { return "SFile"; }
  void save(const std::string& path) const override;

 private:
  SFile(std::string path, bool read_file);
  void parse(const std::string& bytes);
  void validate_for_write() const;
};

}  // namespace eqmdsk
