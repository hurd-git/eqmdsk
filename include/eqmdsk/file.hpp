#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "eqmdsk/field.hpp"

namespace eqmdsk {

class EFITFile {
 public:
  virtual ~EFITFile() = default;

  const std::string& filename() const noexcept { return filename_; }

  void write() const { write(filename_); }
  virtual void write(const std::string& path) const = 0;
  virtual const char* format_name() const noexcept = 0;

 protected:
  explicit EFITFile(std::string filename)
      : filename_(std::move(filename)) {}

  std::string filename_;
};

class FieldFile : public EFITFile {
 public:
  bool contains(const std::string& name) const {
    return fields_.contains(name);
  }
  std::size_t size() const noexcept { return fields_.size(); }
  bool empty() const noexcept { return fields_.empty(); }
  FieldValue& at(const std::string& name) { return fields_.at(name); }
  const FieldValue& at(const std::string& name) const {
    return fields_.at(name);
  }
  FieldValue& operator[](const std::string& name) { return at(name); }
  const FieldValue& operator[](const std::string& name) const {
    return at(name);
  }
  std::vector<std::string> keys() const { return fields_.keys(); }

 protected:
  using EFITFile::EFITFile;

  FieldMap fields_;
};

}  // namespace eqmdsk
