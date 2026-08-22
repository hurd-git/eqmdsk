#pragma once

#include <Eigen/Core>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "eqmdsk/error.hpp"

namespace eqmdsk {

using DoubleVector = Eigen::VectorXd;
using IntVector = Eigen::Matrix<std::int64_t, Eigen::Dynamic, 1>;
using DoubleMatrix =
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using StringVector = std::vector<std::string>;

using FieldValue =
    std::variant<bool, std::int64_t, double, std::string, IntVector,
                 DoubleVector, DoubleMatrix, StringVector>;

struct FieldEntry {
  std::string name;
  FieldValue value;
  bool standard = false;
  std::size_t source_order = 0;
};

class FieldMap {
 public:
  using Container = std::vector<FieldEntry>;
  // Iteration is read-only so callers cannot rename entries and invalidate
  // the map's name-to-index invariant. Values remain mutable through at/set.
  using iterator = Container::const_iterator;
  using const_iterator = Container::const_iterator;

  bool contains(const std::string& name) const noexcept;
  std::size_t size() const noexcept { return entries_.size(); }
  bool empty() const noexcept { return entries_.empty(); }
  FieldMap copy() const { return *this; }

  FieldValue& at(const std::string& name);
  const FieldValue& at(const std::string& name) const;
  FieldValue& operator[](const std::string& name) { return at(name); }
  const FieldValue& operator[](const std::string& name) const {
    return at(name);
  }
  const FieldEntry& entry(const std::string& name) const;

  void insert(std::string name, FieldValue value, bool standard = false,
              std::size_t source_order = 0);
  void assign(std::string name, FieldValue value);
  void set(const std::string& name, FieldValue value);
  bool erase(const std::string& name);
  void clear() noexcept;

  std::vector<std::string> keys() const;

  iterator begin() noexcept { return entries_.cbegin(); }
  iterator end() noexcept { return entries_.cend(); }
  const_iterator begin() const noexcept { return entries_.begin(); }
  const_iterator end() const noexcept { return entries_.end(); }
  const_iterator cbegin() const noexcept { return entries_.cbegin(); }
  const_iterator cend() const noexcept { return entries_.cend(); }

 private:
  Container entries_;
  std::unordered_map<std::string, std::size_t> index_;
};

// A K-file/G-file namelist block is the same compact field mapping used by
// the field-file core.  Keep the descriptive public name without introducing
// a second storage or synchronization layer.
using NamelistBlock = FieldMap;

const char* field_type_name(const FieldValue& value) noexcept;

}  // namespace eqmdsk
