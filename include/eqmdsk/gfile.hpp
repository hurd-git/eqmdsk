#pragma once

#include <optional>
#include <string>

#include "eqmdsk/cocos.hpp"
#include "eqmdsk/file.hpp"

namespace eqmdsk {

class GFile final : public FieldFile {
 public:
  explicit GFile(std::string filename);

  using FieldFile::write;
  const char* format_name() const noexcept override { return "GFile"; }
  void write(const std::string& path) const override;

  const CocosResult& cocos() const noexcept { return cocos_; }
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
};

}  // namespace eqmdsk
