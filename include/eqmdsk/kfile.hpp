#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "eqmdsk/file.hpp"

namespace eqmdsk {

enum class NamelistValueKind {
  null,
  integer,
  real,
  logical,
  string,
  complex,
  raw,
};

class NamelistValue {
 public:
  using Storage =
      std::variant<std::int64_t, double, bool, std::string,
                   std::complex<double>>;

  static NamelistValue null(std::size_t repeat = 1);
  static NamelistValue integer(std::int64_t value,
                               std::size_t repeat = 1);
  static NamelistValue real(double value, std::size_t repeat = 1);
  static NamelistValue logical(bool value, std::size_t repeat = 1);
  static NamelistValue string(std::string value, std::size_t repeat = 1);
  static NamelistValue complex(std::complex<double> value,
                               std::size_t repeat = 1);
  static NamelistValue raw(std::string value, std::size_t repeat = 1);

  NamelistValueKind kind() const noexcept { return kind_; }
  std::size_t repeat() const noexcept { return repeat_; }
  const Storage& storage() const noexcept { return storage_; }
  const std::string& original_text() const noexcept { return original_text_; }

  std::int64_t as_integer() const;
  double as_real() const;
  bool as_logical() const;
  const std::string& as_string() const;
  const std::complex<double>& as_complex() const;
  const std::string& as_raw() const;

  bool operator==(const NamelistValue& other) const noexcept;
  bool operator!=(const NamelistValue& other) const noexcept {
    return !(*this == other);
  }

 private:
  NamelistValue(NamelistValueKind kind, Storage storage, std::size_t repeat,
                std::string original_text = {});

  NamelistValueKind kind_ = NamelistValueKind::raw;
  Storage storage_ = std::string{};
  std::size_t repeat_ = 1;
  std::string original_text_;

  friend class KFile;
};

class NamelistEntry {
 public:
  const std::string& name() const noexcept { return name_; }
  const std::string& original_name() const noexcept { return original_name_; }
  const std::string& designator() const noexcept { return designator_; }
  const std::string& subscript() const noexcept { return subscript_; }
  const std::vector<NamelistValue>& values() const noexcept { return values_; }
  const std::string& raw_text() const noexcept { return raw_text_; }
  std::size_t source_order() const noexcept { return source_order_; }
  std::size_t source_offset() const noexcept { return source_offset_; }
  bool parsed() const noexcept { return parsed_; }
  bool modified() const noexcept;

 private:
  std::string name_;
  std::string original_name_;
  std::string designator_;
  std::string subscript_;
  std::vector<NamelistValue> values_;
  std::vector<NamelistValue> original_values_;
  std::string raw_text_;
  std::string trailing_text_;
  std::string interior_comments_;
  std::size_t source_order_ = 0;
  std::size_t source_offset_ = 0;
  std::size_t relative_begin_ = 0;
  std::size_t relative_end_ = 0;
  bool parsed_ = false;
  bool explicitly_modified_ = false;

  friend class KFile;
};

class NamelistSection {
 public:
  const std::string& name() const noexcept { return name_; }
  const std::string& original_name() const noexcept { return original_name_; }
  char opener() const noexcept { return opener_; }
  const std::string& terminator() const noexcept { return terminator_; }
  const std::vector<NamelistEntry>& entries() const noexcept { return entries_; }
  const std::string& raw_text() const noexcept { return raw_text_; }
  std::size_t source_order() const noexcept { return source_order_; }
  std::size_t source_offset() const noexcept { return source_offset_; }

  std::size_t count(const std::string& name) const;
  const NamelistEntry& entry(const std::string& name,
                             std::size_t occurrence = 0) const;

 private:
  std::string name_;
  std::string original_name_;
  char opener_ = '&';
  std::string terminator_ = "/";
  std::vector<NamelistEntry> entries_;
  std::string raw_text_;
  std::size_t source_order_ = 0;
  std::size_t source_offset_ = 0;

  friend class KFile;
};

class KFile final : public EFITFile {
 public:
  explicit KFile(const std::filesystem::path& filename);

  using EFITFile::write;
  const char* format_name() const noexcept override { return "KFile"; }
  void write(const std::filesystem::path& path) const override;

  const std::vector<NamelistSection>& sections() const noexcept {
    return sections_;
  }
  std::size_t section_count(const std::string& name) const;
  const NamelistSection& section(const std::string& name,
                                 std::size_t occurrence = 0) const;
  const NamelistEntry& entry(const std::string& section_name,
                             const std::string& name,
                             std::size_t occurrence = 0,
                             std::size_t section_occurrence = 0) const;
  void set(const std::string& section_name, const std::string& name,
           std::vector<NamelistValue> values,
           std::size_t occurrence = 0,
           std::size_t section_occurrence = 0);

  // K-file variables are case-insensitive.  These helpers intentionally hide
  // EFITFile's case-sensitive convenience methods while retaining the same
  // return types for ordinary field access.
  bool contains(const std::string& name) const override;
  FieldValue& at(const std::string& name) override;
  const FieldValue& at(const std::string& name) const override;
  std::vector<std::string> keys() const { return fields_.keys(); }

 private:
  struct FieldTarget {
    std::size_t section = 0;
    std::size_t entry = 0;
  };

  void parse(const std::string& bytes);
  void rebuild_fields();
  void refresh_field(const std::string& name);
  std::string serialize_section(std::size_t section_index) const;

  std::vector<NamelistSection> sections_;
  std::vector<std::string> outside_text_;
  std::vector<std::pair<std::string, FieldTarget>> field_targets_;
};

}  // namespace eqmdsk
