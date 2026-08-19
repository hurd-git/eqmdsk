#pragma once

#include <filesystem>
#include <string>

#include "eqmdsk/cocos.hpp"
#include "eqmdsk/file.hpp"

namespace eqmdsk {

class GFile final : public EFITFile {
 public:
  explicit GFile(const std::filesystem::path& filename);

  using EFITFile::write;
  const char* format_name() const noexcept override { return "GFile"; }
  void write(const std::filesystem::path& path) const override;

  const CocosResult& cocos() const noexcept { return cocos_; }
  void select_cocos(int source);
  GFile& to_cocos(int target);
  GFile converted_to_cocos(int target) const;

  const std::string& extra_header() const noexcept { return extra_header_; }
  const std::string& extension_tail() const noexcept { return extension_tail_; }

 private:
  void parse(const std::string& bytes);
  void validate_for_write() const;
  void detect_cocos();

  int idum_ = 0;
  std::string preamble_;
  std::string header_suffix_;
  std::string extra_header_;
  std::string extension_tail_;
  std::size_t original_nw_ = 0;
  std::size_t original_nh_ = 0;
  CocosResult cocos_;
};

}  // namespace eqmdsk
