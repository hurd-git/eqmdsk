#include "eqmdsk/field.hpp"

#include <utility>

namespace eqmdsk {

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
