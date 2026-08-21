#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "eqmdsk/field.hpp"
#include "eqmdsk/file.hpp"

namespace eqmdsk {

namespace detail {
struct KFileImpl;
}

// Reader and writer for EFIT Fortran namelists.
//
// The public model is a mapping from canonical section names to FieldMap
// values. Parser bookkeeping remains private and is discarded after parsing.
class KFile final : public EFITFile {
 public:
  explicit KFile(std::string filename);
  ~KFile() override;

  KFile(const KFile&) = delete;
  KFile& operator=(const KFile&) = delete;
  KFile(KFile&&) noexcept;
  KFile& operator=(KFile&&) noexcept;

  using EFITFile::write;
  const char* format_name() const noexcept override { return "KFile"; }
  void write(const std::string& path) const override;

  bool contains(const std::string& section_name) const;
  std::size_t size() const noexcept;
  bool empty() const noexcept { return size() == 0; }
  FieldMap& at(const std::string& section_name);
  const FieldMap& at(const std::string& section_name) const;
  FieldMap& operator[](const std::string& section_name) {
    return at(section_name);
  }
  const FieldMap& operator[](const std::string& section_name) const {
    return at(section_name);
  }
  std::vector<std::string> keys() const;

 private:
  void parse(const std::string& bytes);

  std::unique_ptr<detail::KFileImpl> impl_;
};

}  // namespace eqmdsk
