#pragma once

#include <cstddef>
#include <string>
#include <utility>

#include "eqmdsk/file.hpp"

namespace eqmdsk {

// Reader and writer for the fixed-record EFIT A-file format.
//
// Standard fields are exposed through the FieldFile mapping using their
// uppercase EFIT names. The file-level header/footer text is also editable;
// the writer emits a canonical A-file around those preserved text regions.
class AFile final : public FieldFile {
 public:
  explicit AFile(std::string path);
  static AFile create();
  AFile copy() const { return *this; }

  using FieldFile::save;
  const std::vector<std::string>& required_fields() const noexcept override;
  const std::vector<std::string>& optional_fields() const noexcept override;
  FieldKind field_kind(const std::string& name) const noexcept override;
  const char* format_name() const noexcept override { return "AFile"; }
  void save(const std::string& path) const override;

  const std::string& header() const noexcept { return header_; }
  void set_header(std::string value);
  const std::string& footer() const noexcept { return footer_; }
  void set_footer(std::string value) { footer_ = std::move(value); }

 private:
  AFile(std::string path, bool read_file);
  void parse(const std::string& bytes);
  void validate_for_write() const;

  std::string header_;
  std::string footer_;
  std::string control_suffix_;
  std::string control_line_ending_ = "\n";
  std::string record_line_ending_ = "\n";
  std::size_t shot_field_offset_ = 0;
  std::size_t time_field_offset_ = std::string::npos;
};

}  // namespace eqmdsk
