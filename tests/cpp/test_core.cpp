#include <cassert>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "eqmdsk/cocos.hpp"
#include "eqmdsk/error.hpp"
#include "eqmdsk/field.hpp"
#include "eqmdsk/afile.hpp"
#include "eqmdsk/gfile.hpp"
#include "eqmdsk/kfile.hpp"
#include "eqmdsk/sfile.hpp"

int main() {
  static_assert(std::is_base_of_v<eqmdsk::Namelist, eqmdsk::KFile>);
  static_assert(std::is_same_v<
                decltype(std::declval<eqmdsk::GFile&>().aux_namelist()),
                eqmdsk::Namelist*>);
  static_assert(std::is_const_v<std::remove_reference_t<
                decltype(*std::declval<eqmdsk::FieldMap&>().begin())>>);

  eqmdsk::FieldMap fields;
  fields.insert("NW", std::int64_t{65}, true);
  fields.insert("PSIRZ", eqmdsk::DoubleMatrix(eqmdsk::DoubleMatrix::Zero(3, 4)),
                true);

  assert(fields.contains("NW"));
  assert(!fields.contains("nw"));
  assert(std::get<std::int64_t>(fields.at("NW")) == 65);
  assert(std::get<eqmdsk::DoubleMatrix>(fields.at("PSIRZ")).IsRowMajor);
  const auto fields_copy = fields.copy();
  assert(std::get<std::int64_t>(fields_copy.at("NW")) == 65);

  bool rejected_duplicate = false;
  try {
    fields.insert("NW", std::int64_t{66});
  } catch (const eqmdsk::FieldError&) {
    rejected_duplicate = true;
  }
  assert(rejected_duplicate);

  const eqmdsk::CocosResult unique(11, {11}, "test");
  assert(unique.is_unique());
  assert(unique.selected() == 11);

  const eqmdsk::CocosResult ambiguous({1, 11}, "test");
  assert(ambiguous.is_ambiguous());
  bool rejected_selection = false;
  try {
    static_cast<void>(ambiguous.selected());
  } catch (const eqmdsk::CocosError&) {
    rejected_selection = true;
  }
  assert(rejected_selection);

  const auto gfile = eqmdsk::GFile::create(4, 3);
  assert(std::get<std::int64_t>(gfile.at("NW")) == 4);
  assert(std::get<eqmdsk::DoubleMatrix>(gfile.at("PSIRZ")).rows() == 3);
  assert(gfile.aux_namelist() != nullptr);
  assert(gfile.aux_namelist()->empty());
  const auto gfile_copy = gfile.copy();
  assert(std::get<std::int64_t>(gfile_copy.at("NW")) == 4);
  auto editable_gfile = eqmdsk::GFile::create(4, 3);
  editable_gfile.assign("RBBBS", eqmdsk::DoubleVector::Ones(2).eval());
  assert(std::get<eqmdsk::DoubleVector>(editable_gfile.at("RBBBS")).size() ==
         2);
  bool rejected_derived_grid = false;
  try {
    editable_gfile.assign("RGRID", eqmdsk::DoubleVector::Ones(2).eval());
  } catch (const eqmdsk::FieldError&) {
    rejected_derived_grid = true;
  }
  assert(rejected_derived_grid);
  const auto afile = eqmdsk::AFile::create();
  assert(afile.contains("SHOT"));
  const auto afile_copy = afile.copy();
  assert(afile_copy.contains("SHOT"));
  const auto sfile = eqmdsk::SFile::create(7);
  assert(std::get<eqmdsk::DoubleVector>(sfile.at("X")).size() == 7);
  const auto sfile_copy = sfile.copy();
  assert(std::get<eqmdsk::DoubleVector>(sfile_copy.at("X")).size() == 7);
  assert(sfile.filename().empty());
  assert(sfile.path().empty());
  assert(sfile.abspath().empty());

  const auto has_name = [](const std::vector<std::string>& names,
                           const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
  };
  assert(has_name(sfile.missing_fields(), "X"));
  assert(has_name(sfile.missing_optional_fields(), "TITLE"));

  bool rejected_unknown_gfield = false;
  try {
    auto editable = eqmdsk::GFile::create(1, 1);
    editable.assign("UNKNOWN", std::int64_t{1});
  } catch (const eqmdsk::FieldError&) {
    rejected_unknown_gfield = true;
  }
  assert(rejected_unknown_gfield);

  bool rejected_unknown_afield = false;
  try {
    auto editable = eqmdsk::AFile::create();
    editable.assign("UNKNOWN", std::int64_t{1});
  } catch (const eqmdsk::FieldError&) {
    rejected_unknown_afield = true;
  }
  assert(rejected_unknown_afield);

  auto editable_sfile = eqmdsk::SFile::create(1);
  editable_sfile.assign("X", eqmdsk::DoubleVector::Ones(1).eval());
  assert(!editable_sfile.is_missing_field("X"));
  assert(editable_sfile.erase("X"));
  assert(editable_sfile.contains("X"));
  assert(editable_sfile.is_missing_field("X"));
  editable_sfile.assign("X", eqmdsk::DoubleVector::Ones(1).eval());
  assert(!editable_sfile.is_missing_field("X"));

  editable_sfile.assign("TITLE", std::string{"created"});
  assert(editable_sfile.contains("TITLE"));
  assert(!has_name(editable_sfile.missing_optional_fields(), "TITLE"));
  assert(editable_sfile.erase("TITLE"));
  assert(!editable_sfile.contains("TITLE"));
  assert(!has_name(editable_sfile.missing_optional_fields(), "TITLE"));

  bool rejected_unknown_sfield = false;
  try {
    editable_sfile.assign("UNKNOWN", std::int64_t{1});
  } catch (const eqmdsk::FieldError&) {
    rejected_unknown_sfield = true;
  }
  assert(rejected_unknown_sfield);

  bool rejected_missing_required = false;
  try {
    editable_sfile.save("missing-required.s");
  } catch (const eqmdsk::ValidationError&) {
    rejected_missing_required = true;
  }
  assert(rejected_missing_required);

  bool rejected_empty_save = false;
  try {
    sfile.save();
  } catch (const eqmdsk::ValidationError&) {
    rejected_empty_save = true;
  }
  assert(rejected_empty_save);
  auto kfile = eqmdsk::KFile::create();
  kfile.assign_block("IN1");
  eqmdsk::NamelistBlock& block = kfile["IN1"];
  block.assign("LIMITR", std::int64_t{60});
  assert(std::get<std::int64_t>(block.at("LIMITR")) == 60);
  assert(kfile.erase_block("IN1"));
  assert(kfile.empty());
  const auto kfile_copy = kfile.copy();
  assert(kfile_copy.empty());

  // Namelist is a concrete path-independent object, not a KFile helper or
  // wrapper.  File persistence belongs to KFile/GFile, so copy it into a
  // KFile before saving.
  auto namelist = eqmdsk::Namelist::create();
  namelist.assign_block("OUT1");
  namelist["OUT1"].assign("VALUE", std::int64_t{2});
  assert(namelist.contains("OUT1"));
  assert(std::get<std::int64_t>(namelist["OUT1"]["VALUE"]) == 2);
  const auto namelist_copy = namelist.copy();
  assert(std::get<std::int64_t>(namelist_copy["OUT1"]["VALUE"]) == 2);
  auto namelist_file = eqmdsk::KFile::create();
  static_cast<eqmdsk::Namelist&>(namelist_file) = namelist;
  const auto namelist_path =
      std::filesystem::temp_directory_path() / "eqmdsk-namelist";
  namelist_file.save(namelist_path.string());
  const eqmdsk::KFile reparsed_namelist(namelist_path.string());
  assert(reparsed_namelist.contains("OUT1"));
  assert(std::get<std::int64_t>(reparsed_namelist["OUT1"]["VALUE"]) == 2);
  assert(namelist.erase_block("OUT1"));
  assert(namelist.empty());
  std::error_code ignored;
  std::filesystem::remove(namelist_path, ignored);
}
