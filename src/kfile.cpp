#include "eqmdsk/kfile.hpp"

#include <sstream>
#include <utility>

#include "detail/fortran.hpp"

namespace eqmdsk {

KFile::KFile(std::string path) : KFile(std::move(path), true) {}

KFile::KFile(std::string path, bool read_file)
    : EFITFile(std::move(path)), Namelist(path_) {
  if (read_file) {
    parse(detail::read_binary_file(path_), path_);
  }
}

KFile KFile::create() { return KFile({}, false); }

KFile::~KFile() = default;
KFile::KFile(const KFile& other)
    : EFITFile(other), Namelist(other) {}
KFile& KFile::operator=(const KFile& other) {
  if (this != &other) {
    filename_ = other.filename_;
    path_ = other.path_;
    abspath_ = other.abspath_;
    Namelist::operator=(other);
  }
  return *this;
}
KFile::KFile(KFile&&) noexcept = default;
KFile& KFile::operator=(KFile&&) noexcept = default;

void KFile::save(const std::string& path) const {
  std::ostringstream output;
  write_to(output);
  detail::write_binary_file(path, output.str());
}

}  // namespace eqmdsk
