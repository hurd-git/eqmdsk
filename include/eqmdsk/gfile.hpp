#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eqmdsk/cocos.hpp"
#include "eqmdsk/file.hpp"

namespace eqmdsk {

class KFile;

class GFile final : public FieldFile {
 public:
  explicit GFile(std::string filename);
  ~GFile() override;

  GFile(const GFile& other);
  GFile& operator=(const GFile& other);
  GFile(GFile&&) noexcept;
  GFile& operator=(GFile&&) noexcept;

  using FieldFile::write;
  const char* format_name() const noexcept override { return "GFile"; }
  void write(const std::string& path) const override;

  const CocosResult& cocos() const noexcept { return cocos_; }
  KFile* aux_namelist() noexcept { return aux_namelist_.get(); }
  const KFile* aux_namelist() const noexcept { return aux_namelist_.get(); }
  void select_cocos(int source);
  GFile& to_cocos(int target, std::optional<int> from_cocos = std::nullopt);
  GFile converted_to_cocos(
      int target, std::optional<int> from_cocos = std::nullopt) const;

 private:
  void parse(const std::string& bytes);
  void validate_for_write() const;
  void detect_cocos();

  int idum_ = 0;
  CocosResult cocos_;
  std::unique_ptr<KFile> aux_namelist_;
};

}  // namespace eqmdsk
