#include "eqmdsk/field.hpp"
#include "eqmdsk/file.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <utility>

namespace eqmdsk {

namespace {

const char* field_kind_name(FieldKind kind) noexcept {
  switch (kind) {
    case FieldKind::Boolean:
      return "bool";
    case FieldKind::Integer:
      return "int";
    case FieldKind::Real:
      return "float";
    case FieldKind::String:
      return "str";
    case FieldKind::IntegerVector:
      return "int64 array";
    case FieldKind::RealVector:
      return "float64 array";
    case FieldKind::RealMatrix:
      return "2-D float64 array";
    case FieldKind::StringVector:
      return "list[str]";
    case FieldKind::Any:
    default:
      return "a supported value";
  }
}

bool has_expected_type(const FieldValue& value, FieldKind kind) noexcept {
  switch (kind) {
    case FieldKind::Boolean:
      return std::holds_alternative<bool>(value);
    case FieldKind::Integer:
      return std::holds_alternative<std::int64_t>(value);
    case FieldKind::Real:
      return std::holds_alternative<double>(value);
    case FieldKind::String:
      return std::holds_alternative<std::string>(value);
    case FieldKind::IntegerVector:
      return std::holds_alternative<IntVector>(value);
    case FieldKind::RealVector:
      return std::holds_alternative<DoubleVector>(value);
    case FieldKind::RealMatrix:
      return std::holds_alternative<DoubleMatrix>(value);
    case FieldKind::StringVector:
      return std::holds_alternative<StringVector>(value);
    case FieldKind::Any:
    default:
      return true;
  }
}

}  // namespace

void EFITFile::set_path(std::string value) {
  if (value.empty()) {
    filename_.clear();
    path_.clear();
    abspath_.clear();
    return;
  }
  // File paths cross the C++/Python boundary as UTF-8 bytes.  u8path keeps
  // the same representation on Windows and POSIX instead of using a locale
  // conversion for path(std::string).
  std::filesystem::path input = std::filesystem::u8path(value);
  if (input.filename().empty()) {
    throw FieldError("path must name a file");
  }
  if (!input.has_parent_path()) {
    input = std::filesystem::path(".") / input;
  }
  path_ = input.u8string();
  filename_ = input.filename().u8string();
  abspath_ = std::filesystem::absolute(input).lexically_normal().u8string();
}

void FieldFile::assign(std::string name, FieldValue value) {
  if (!is_known_field(name)) {
    throw FieldError("unknown field: " + name);
  }
  const auto expected = field_kind(name);
  if (expected == FieldKind::Real &&
      std::holds_alternative<std::int64_t>(value)) {
    const auto integer = std::get<std::int64_t>(value);
    constexpr auto max_exact_integer =
        std::int64_t{1} << std::numeric_limits<double>::digits;
    if (integer > max_exact_integer || integer < -max_exact_integer) {
      throw FieldError(name + " integer value cannot be represented exactly as a float");
    }
    value = static_cast<double>(integer);
  }
  if (expected == FieldKind::RealVector &&
      std::holds_alternative<IntVector>(value)) {
    const auto& source = std::get<IntVector>(value);
    DoubleVector converted(source.size());
    for (Eigen::Index index = 0; index < source.size(); ++index) {
      converted[index] = static_cast<double>(source[index]);
    }
    value = std::move(converted);
  }
  if (expected != FieldKind::Any && !has_expected_type(value, expected)) {
    throw FieldError(name + " expects " + field_kind_name(expected));
  }
  fields_.assign(name, std::move(value));
  clear_missing(name);
}

bool FieldFile::is_known_field(const std::string& name) const noexcept {
  const auto contains = [&name](const std::vector<std::string>& fields) {
    return std::find(fields.begin(), fields.end(), name) != fields.end();
  };
  return contains(required_fields()) || contains(optional_fields());
}

bool FieldFile::is_required_field(const std::string& name) const noexcept {
  const auto& fields = required_fields();
  return std::find(fields.begin(), fields.end(), name) != fields.end();
}

bool FieldFile::is_missing_field(const std::string& name) const noexcept {
  return missing_fields_.find(name) != missing_fields_.end();
}

std::vector<std::string> FieldFile::schema_fields() const {
  std::vector<std::string> result;
  result.reserve(required_fields().size() + optional_fields().size());
  result.insert(result.end(), required_fields().begin(),
                required_fields().end());
  result.insert(result.end(), optional_fields().begin(),
                optional_fields().end());
  return result;
}

std::vector<std::string> FieldFile::missing_fields() const {
  std::vector<std::string> result;
  for (const auto& name : required_fields()) {
    if (is_missing_field(name)) {
      result.push_back(name);
    }
  }
  return result;
}

std::vector<std::string> FieldFile::missing_optional_fields() const {
  std::vector<std::string> result;
  for (const auto& name : optional_fields()) {
    if (is_missing_field(name)) {
      result.push_back(name);
    }
  }
  return result;
}

void FieldFile::mark_present(const std::string& name) {
  if (!is_known_field(name)) {
    throw FieldError("unknown field: " + name);
  }
  if (!fields_.contains(name)) {
    throw FieldError("field has no value: " + name);
  }
  clear_missing(name);
}

bool FieldFile::erase(const std::string& name) {
  if (!is_known_field(name)) {
    throw FieldError("unknown field: " + name);
  }
  if (is_required_field(name)) {
    const bool was_present = !is_missing_field(name);
    missing_fields_.insert(name);
    return was_present;
  }

  const bool was_missing = is_missing_field(name);
  clear_missing(name);
  return fields_.erase(name) || was_missing;
}

void FieldFile::validate_required_fields() const {
  const auto missing = missing_fields();
  if (missing.empty()) {
    return;
  }

  std::ostringstream message;
  message << format_name() << " is missing required fields: ";
  for (std::size_t index = 0; index < missing.size(); ++index) {
    if (index != 0) {
      message << ", ";
    }
    message << missing[index];
  }
  throw ValidationError(message.str());
}

void FieldFile::mark_all_fields_missing() {
  missing_fields_.clear();
  for (const auto& name : required_fields()) {
    missing_fields_.insert(name);
  }
  for (const auto& name : optional_fields()) {
    missing_fields_.insert(name);
  }
}

void FieldFile::clear_missing(const std::string& name) {
  missing_fields_.erase(name);
}

bool FieldMap::contains(const std::string& name) const noexcept {
  return index_.find(name) != index_.end();
}

const FieldEntry& FieldMap::entry(const std::string& name) const {
  const auto found = index_.find(name);
  if (found == index_.end()) {
    throw FieldError("unknown field: " + name);
  }
  return entries_[found->second];
}

FieldValue& FieldMap::at(const std::string& name) {
  const auto found = index_.find(name);
  if (found == index_.end()) {
    throw FieldError("unknown field: " + name);
  }
  return entries_[found->second].value;
}

const FieldValue& FieldMap::at(const std::string& name) const {
  return entry(name).value;
}

void FieldMap::insert(std::string name, FieldValue value, bool standard,
                      std::size_t source_order) {
  if (name.empty()) {
    throw FieldError("field name must not be empty");
  }
  if (contains(name)) {
    throw FieldError("duplicate field: " + name);
  }
  const auto position = entries_.size();
  entries_.push_back(
      FieldEntry{std::move(name), std::move(value), standard, source_order});
  index_.emplace(entries_.back().name, position);
}

void FieldMap::assign(std::string name, FieldValue value) {
  if (contains(name)) {
    auto& current = at(name);
    if (std::holds_alternative<std::int64_t>(current) &&
        std::holds_alternative<double>(value)) {
      throw FieldError(name + " expects int");
    }
    if (std::holds_alternative<bool>(current) &&
        !std::holds_alternative<bool>(value)) {
      throw FieldError(name + " expects bool");
    }
    if (std::holds_alternative<double>(current) &&
        std::holds_alternative<std::int64_t>(value)) {
      value = static_cast<double>(std::get<std::int64_t>(value));
    }
    if (std::holds_alternative<double>(current) &&
        !std::holds_alternative<double>(value)) {
      throw FieldError(name + " expects float");
    }
    if (std::holds_alternative<DoubleVector>(current) &&
        std::holds_alternative<IntVector>(value)) {
      const auto& source = std::get<IntVector>(value);
      DoubleVector converted(source.size());
      for (Eigen::Index index = 0; index < source.size(); ++index) {
        converted[index] = static_cast<double>(source[index]);
      }
      value = std::move(converted);
    }
    if (std::holds_alternative<IntVector>(current) &&
        std::holds_alternative<DoubleVector>(value)) {
      throw FieldError(name + " expects integer array");
    }
    if (current.index() != value.index() &&
        !std::holds_alternative<double>(current)) {
      throw FieldError(name + " has an incompatible value type");
    }
    set(name, std::move(value));
  } else {
    insert(std::move(name), std::move(value));
  }
}

void FieldMap::set(const std::string& name, FieldValue value) {
  at(name) = std::move(value);
}

bool FieldMap::erase(const std::string& name) {
  const auto found = index_.find(name);
  if (found == index_.end()) {
    return false;
  }
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(found->second));
  index_.clear();
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    index_.emplace(entries_[i].name, i);
  }
  return true;
}

void FieldMap::clear() noexcept {
  entries_.clear();
  index_.clear();
}

std::vector<std::string> FieldMap::keys() const {
  std::vector<std::string> result;
  result.reserve(entries_.size());
  for (const auto& item : entries_) {
    result.push_back(item.name);
  }
  return result;
}

const char* field_type_name(const FieldValue& value) noexcept {
  switch (value.index()) {
    case 0:
      return "bool";
    case 1:
      return "int";
    case 2:
      return "float";
    case 3:
      return "str";
    case 4:
      return "int_vector";
    case 5:
      return "float_vector";
    case 6:
      return "float_matrix";
    case 7:
      return "str_vector";
    default:
      return "unknown";
  }
}

}  // namespace eqmdsk
