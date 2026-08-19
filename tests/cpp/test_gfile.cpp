#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <locale>
#include <sstream>
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

std::string input_real(double value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::uppercase << std::scientific << std::setprecision(9)
         << std::setw(16) << value;
  return output.str();
}

void append_reals(std::string& output,
                  std::initializer_list<double> values) {
  for (const auto value : values) {
    output += input_real(value);
  }
  output += '\n';
}

std::string synthetic_gfile() {
  std::ostringstream header;
  header.imbue(std::locale::classic());
  header << std::left << std::setw(48) << "SYNTHETIC NONSQUARE" << std::right
         << std::setw(4) << 0 << std::setw(4) << 3 << std::setw(4) << 2
         << '\n';
  std::string output = header.str();
  append_reals(output, {1, 2, 3, 4, 5});
  append_reals(output, {6, 7, -0.5, 0.5, -2});
  append_reals(output, {4, -0.5, 0, 6, 0});
  append_reals(output, {7, 0, 0.5, 0, 0});
  append_reals(output, {1, 2, 3});
  append_reals(output, {4, 5, 6});
  append_reals(output, {7, 8, 9});
  append_reals(output, {10, 11, 12});
  append_reals(output, {100, 101, 102, 200, 201});
  append_reals(output, {202});
  append_reals(output, {1, 1.5, 2});
  output += "    2    1\n";
  append_reals(output, {1, 10, 2, 20});
  append_reals(output, {3, 30});
  constexpr char tail[] = "&EXTRA\n VALUE=1\n/\n\0MAG\n";
  output.append(tail, sizeof(tail) - 1);
  return output;
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main() {
  const auto synthetic_path =
      std::filesystem::current_path() / "gfile-synthetic.test";
  const auto synthetic_output =
      std::filesystem::current_path() / "gfile-synthetic-output.test";
  write_bytes(synthetic_path, synthetic_gfile());
  eqmdsk::GFile synthetic(synthetic_path);
  assert(get<std::int64_t>(synthetic, "NW") == 3);
  assert(get<std::int64_t>(synthetic, "NH") == 2);
  assert(get<eqmdsk::DoubleMatrix>(synthetic, "PSIRZ")(1, 2) == 202.0);
  assert(synthetic.extension_tail().find('\0') != std::string::npos);
  std::get<double>(synthetic.at("CURRENT")) = -1.0e100;
  synthetic.write(synthetic_output);
  eqmdsk::GFile synthetic_reparsed(synthetic_output);
  assert(close(get<double>(synthetic_reparsed, "CURRENT"), -1.0e100, 1e-9));
  assert(synthetic_reparsed.extension_tail() == synthetic.extension_tail());
  std::filesystem::remove(synthetic_path);
  std::filesystem::remove(synthetic_output);

  const auto source =
      std::filesystem::path(EQMDSK_LOCAL_DATA_DIR) / "g067590.03300";
  if (!std::filesystem::exists(source)) {
    return 0;  // The self-contained compatibility test above always ran.
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
