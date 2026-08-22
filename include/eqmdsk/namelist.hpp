#pragma once

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "eqmdsk/field.hpp"

namespace eqmdsk {

namespace detail {
struct NamelistImpl;
}

// A concrete, path-independent Fortran namelist object.  GFile owns one
// instance for its AuxNamelist and KFile derives from this class to add file
// path and file-level save behavior.
class Namelist {
 public:
  Namelist();
  static Namelist create();
  virtual ~Namelist();

  Namelist(const Namelist& other);
  Namelist& operator=(const Namelist& other);
  Namelist(Namelist&&) noexcept;
  Namelist& operator=(Namelist&&) noexcept;

  bool contains(const std::string& block_name) const;
  std::size_t size() const noexcept;
  bool empty() const noexcept { return size() == 0; }
  Namelist copy() const { return *this; }
  NamelistBlock& at(const std::string& block_name);
  const NamelistBlock& at(const std::string& block_name) const;
  NamelistBlock& operator[](const std::string& block_name) {
    return at(block_name);
  }
  const NamelistBlock& operator[](const std::string& block_name) const {
    return at(block_name);
  }
  std::vector<std::string> keys() const;
  void assign_block(const std::string& block_name);
  bool erase_block(const std::string& block_name);

 protected:
  explicit Namelist(std::string diagnostic_path);
  void parse(const std::string& bytes, const std::string& diagnostic_path);

 private:
  friend class KFile;
  friend class GFile;
  std::string serialize() const;
  void write_to(std::ostream& output) const;
  std::unique_ptr<detail::NamelistImpl> impl_;
};

}  // namespace eqmdsk
