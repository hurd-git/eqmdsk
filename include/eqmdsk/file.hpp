#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "eqmdsk/field.hpp"

namespace eqmdsk {

enum class FieldKind {
  Any,
  Boolean,
  Integer,
  Real,
  String,
  IntegerVector,
  RealVector,
  RealMatrix,
  StringVector,
};

class EFITFile {
 public:
  virtual ~EFITFile() = default;

  const std::string& filename() const noexcept { return filename_; }
  const std::string& path() const noexcept { return path_; }
  const std::string& abspath() const noexcept { return abspath_; }

  void save() const {
    if (path_.empty()) {
      throw ValidationError("save requires a path for an object created without a path");
    }
    save(abspath_);
  }
  virtual void save(const std::string& path) const = 0;
  virtual const char* format_name() const noexcept = 0;

 protected:
  explicit EFITFile(std::string path) { set_path(std::move(path)); }

  void set_path(std::string value);

  std::string filename_;
  std::string path_;
  std::string abspath_;
};

class FieldFile : public EFITFile {
 public:
  virtual const std::vector<std::string>& required_fields() const noexcept = 0;
  virtual const std::vector<std::string>& optional_fields() const noexcept = 0;
  virtual FieldKind field_kind(const std::string& name) const noexcept {
    static_cast<void>(name);
    return FieldKind::Any;
  }
  bool contains(const std::string& name) const {
    return fields_.contains(name);
  }
  bool is_known_field(const std::string& name) const noexcept;
  bool is_required_field(const std::string& name) const noexcept;
  bool is_missing_field(const std::string& name) const noexcept;
  std::vector<std::string> schema_fields() const;
  std::vector<std::string> missing_fields() const;
  std::vector<std::string> missing_optional_fields() const;

  // assign() is the mutation entry point for replacing a field or restoring
  // one that is currently missing. erase() marks required fields as missing
  // and physically removes optional fields.
  virtual void assign(std::string name, FieldValue value);
  void mark_present(const std::string& name);
  bool erase(const std::string& name);
  void validate_required_fields() const;
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

  void mark_all_fields_missing();
  void clear_missing(const std::string& name);

  FieldMap fields_;
  std::unordered_set<std::string> missing_fields_;
};

}  // namespace eqmdsk
