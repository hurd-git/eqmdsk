#pragma once

#include <cstddef>
#include <string>

#include "eqmdsk/file.hpp"

namespace eqmdsk {

// Reader and writer for the fixed-record EFIT A-file format.
//
// Standard fields are exposed through the FieldFile mapping using their
// uppercase EFIT names. The writer emits a canonical A-file from those fields.
class AFile final : public FieldFile {
 public:
  explicit AFile(std::string filename);

  using FieldFile::write;
  const char* format_name() const noexcept override { return "AFile"; }
  void write(const std::string& path) const override;

 private:
  void parse(const std::string& bytes);
  void validate_for_write() const;

  std::string date_header_;
  std::string shot_suffix_;
};

}  // namespace eqmdsk
