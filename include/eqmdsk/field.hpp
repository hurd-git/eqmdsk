#pragma once

#include <Eigen/Core>

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
  using iterator = Container::iterator;
  using const_iterator = Container::const_iterator;

  bool contains(const std::string& name) const noexcept;
  std::size_t size() const noexcept { return entries_.size(); }
  bool empty() const noexcept { return entries_.empty(); }

  FieldValue& at(const std::string& name);
  const FieldValue& at(const std::string& name) const;
  FieldEntry& entry(const std::string& name);
  const FieldEntry& entry(const std::string& name) const;

  void insert(std::string name, FieldValue value, bool standard = false,
              std::size_t source_order = 0);
  void set(const std::string& name, FieldValue value);
  bool erase(const std::string& name);
  void clear() noexcept;

  std::vector<std::string> keys() const;

  iterator begin() noexcept { return entries_.begin(); }
  iterator end() noexcept { return entries_.end(); }
  const_iterator begin() const noexcept { return entries_.begin(); }
  const_iterator end() const noexcept { return entries_.end(); }
  const_iterator cbegin() const noexcept { return entries_.cbegin(); }
  const_iterator cend() const noexcept { return entries_.cend(); }

 private:
  Container entries_;
  std::unordered_map<std::string, std::size_t> index_;
};

const char* field_type_name(const FieldValue& value) noexcept;

}  // namespace eqmdsk

