#pragma once

#include <string>

#include "eqmdsk/file.hpp"
#include "eqmdsk/namelist.hpp"

namespace eqmdsk {

// Reader and writer for EFIT Fortran namelist files.
class KFile final : public EFITFile, public Namelist {
 public:
  explicit KFile(std::string path);
  static KFile create();
  ~KFile() override;

  KFile(const KFile& other);
  KFile& operator=(const KFile& other);
  KFile(KFile&&) noexcept;
  KFile& operator=(KFile&&) noexcept;
  KFile copy() const { return *this; }

  using EFITFile::save;
  using Namelist::at;
  using Namelist::assign_block;
  using Namelist::contains;
  using Namelist::empty;
  using Namelist::erase_block;
  using Namelist::keys;
  using Namelist::operator[];
  using Namelist::size;
  const char* format_name() const noexcept override { return "KFile"; }
  void save(const std::string& path) const override;

 private:
  KFile(std::string path, bool read_file);
};

}  // namespace eqmdsk
