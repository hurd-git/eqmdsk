#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "eqmdsk/file.hpp"

namespace eqmdsk {

// Reader and writer for the fixed-record EFIT A-file format.
//
// Standard fields are exposed through EFITFile::fields() using their
// uppercase EFIT names.  Text before the '*' control record and data after
// the last recognized optional record are retained as opaque binary strings.
class AFile final : public EFITFile {
 public:
  explicit AFile(const std::filesystem::path& filename);

  using EFITFile::write;
  const char* format_name() const noexcept override { return "AFile"; }
  void write(const std::filesystem::path& path) const override;

  const std::string& header() const noexcept { return header_; }
  const std::string& footer() const noexcept { return footer_; }
  std::size_t optional_record_count() const noexcept;

 private:
  void parse(const std::string& bytes);
  void validate_for_write() const;

  std::string header_;
  std::string control_suffix_;
  std::string control_line_ending_ = "\n";
  std::string record_line_ending_ = "\n";
  std::string footer_;
  std::size_t shot_field_offset_ = 0;
  std::size_t time_field_offset_ = std::string::npos;
};

}  // namespace eqmdsk
