#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "eqmdsk/gfile.hpp"

namespace {

template <typename T>
const T& get(const eqmdsk::GFile& file, const std::string& name) {
  return std::get<T>(file.at(name));
}

bool close(double left, double right, double tolerance = 1e-12) {
  return std::abs(left - right) <= tolerance * std::max(1.0, std::abs(right));
}

}  // namespace

int main() {
  const auto source =
      std::filesystem::path(EQMDSK_LOCAL_DATA_DIR) / "g067590.03300";
  if (!std::filesystem::exists(source)) {
    return 0;
  }

  const eqmdsk::GFile original(source);
  assert(get<std::int64_t>(original, "NW") == 129);
  assert(get<std::int64_t>(original, "NH") == 129);
  assert(get<std::int64_t>(original, "NBBBS") == 94);
  assert(get<std::int64_t>(original, "LIMITR") == 61);
  const auto& psi = get<eqmdsk::DoubleMatrix>(original, "PSIRZ");
  assert(psi.rows() == 129 && psi.cols() == 129);
  assert(close(psi(0, 0), -0.509599630));
  assert(close(psi(64, 64), -0.535062553));
  assert(close(psi(128, 128), -0.0116863028));
  assert(original.extension_tail().size() == 383000);
  assert(original.extension_tail().find("&OUT1") != std::string::npos);
  assert((original.cocos().candidates() == std::vector<int>{5, 6, 15, 16}));

  const auto target = std::filesystem::current_path() / "gfile-roundtrip.test";
  original.write(target);
  const eqmdsk::GFile reparsed(target);
  assert(reparsed.extension_tail() == original.extension_tail());
  assert(get<eqmdsk::DoubleMatrix>(reparsed, "PSIRZ").isApprox(psi, 2e-9));
  std::filesystem::remove(target);
}
