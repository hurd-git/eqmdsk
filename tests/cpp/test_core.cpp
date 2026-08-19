#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "eqmdsk/cocos.hpp"
#include "eqmdsk/error.hpp"
#include "eqmdsk/field.hpp"

int main() {
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
}
