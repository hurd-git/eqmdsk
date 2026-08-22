#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eqmdsk/cocos.hpp"
#include "eqmdsk/file.hpp"
#include "eqmdsk/namelist.hpp"

namespace eqmdsk {

class GFile final : public FieldFile {
 public:
  explicit GFile(std::string path);
  static GFile create(std::size_t nw, std::size_t nh);
  ~GFile() override;

  GFile(const GFile& other);
  GFile& operator=(const GFile& other);
  GFile(GFile&&) noexcept;
  GFile& operator=(GFile&&) noexcept;
  GFile copy() const { return *this; }

  using FieldFile::save;
  const std::vector<std::string>& required_fields() const noexcept override;
  const std::vector<std::string>& optional_fields() const noexcept override;
  FieldKind field_kind(const std::string& name) const noexcept override;
  void assign(std::string name, FieldValue value) override;
  const char* format_name() const noexcept override { return "GFile"; }
  void save(const std::string& path) const override;

  const CocosResult& cocos() const noexcept { return cocos_; }
  Namelist* aux_namelist() noexcept { return aux_namelist_.get(); }
  const Namelist* aux_namelist() const noexcept { return aux_namelist_.get(); }
  void select_cocos(int source);
  GFile& to_cocos(int target, std::optional<int> from_cocos = std::nullopt);
  GFile converted_to_cocos(
      int target, std::optional<int> from_cocos = std::nullopt) const;

 private:
  GFile(std::string path, bool read_file);
  void parse(const std::string& bytes);
  void validate_for_write() const;
  void detect_cocos();
  void update_derived_grids();

  int idum_ = 0;
  CocosResult cocos_;
  std::unique_ptr<Namelist> aux_namelist_;
};

}  // namespace eqmdsk
