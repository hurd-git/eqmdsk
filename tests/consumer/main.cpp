#include <cstdint>
#include <iostream>

#include <eqmdsk/eqmdsk.hpp>

int main() {
  eqmdsk::FieldMap fields;
  fields.insert("NW", std::int64_t{3}, true);
  if (!fields.contains("NW") || fields.contains("nw")) {
    return 1;
  }
  std::cout << eqmdsk::version_string << '\n';
  return 0;
}

