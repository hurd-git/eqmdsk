#pragma once

#include <cstddef>
#include <string>

namespace eqmdsk {

struct RawSection {
  std::string name;
  std::string data;
  std::size_t source_offset = 0;
  bool modified = false;
};

}  // namespace eqmdsk

