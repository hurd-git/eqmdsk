#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "eqmdsk/field.hpp"
#include "eqmdsk/raw_section.hpp"

namespace eqmdsk {

class EFITFile {
 public:
  virtual ~EFITFile() = default;

  const std::filesystem::path& filename() const noexcept { return filename_; }
  FieldMap& fields() noexcept { return fields_; }
  const FieldMap& fields() const noexcept { return fields_; }
  const std::vector<RawSection>& raw_sections() const noexcept {
    return raw_sections_;
  }

  virtual bool contains(const std::string& name) const {
    return fields_.contains(name);
  }
  virtual FieldValue& at(const std::string& name) { return fields_.at(name); }
  virtual const FieldValue& at(const std::string& name) const {
    return fields_.at(name);
  }
  std::vector<std::string> keys() const { return fields_.keys(); }

  void write() const { write(filename_); }
  virtual void write(const std::filesystem::path& path) const = 0;
  virtual const char* format_name() const noexcept = 0;

 protected:
  explicit EFITFile(std::filesystem::path filename)
      : filename_(std::move(filename)) {}

  std::filesystem::path filename_;
  FieldMap fields_;
  std::vector<RawSection> raw_sections_;
};

}  // namespace eqmdsk
