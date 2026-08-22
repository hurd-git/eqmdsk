#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>

#include "eqmdsk/gfile.hpp"
#include "eqmdsk/kfile.hpp"

namespace {
template <typename T>
const T& get(const eqmdsk::GFile& file, const char* name) {
  return std::get<T>(file.at(name));
}

std::string synthetic_gfile() {
  auto line = [](std::initializer_list<double> values) {
    std::string result;
    for (const auto value : values) {
      char buffer[32];
      std::snprintf(buffer, sizeof(buffer), "%16.9E", value);
      result += buffer;
    }
    return result + "\n";
  };
  std::string output = "SYNTHETIC GFILE";
  output.resize(48, ' ');
  output += "   0   3   2\n";
  output += line({1, 2, 3, 4, 5});
  output += line({6, 7, -0.5, 0.5, -2});
  output += line({4, -0.5, 0, 6, 0});
  output += line({7, 0, 0.5, 0, 0});
  output += line({1, 2, 3}) + line({4, 5, 6}) + line({7, 8, 9}) +
            line({10, 11, 12});
  output += line({100, 101, 102, 200, 201}) + line({202});
  output += line({1, 1.5, 2}) + "    2    1\n" + line({1, 10, 2, 20}) +
            line({3, 30});
  return output;
}

void write_file(const std::filesystem::path& path, const std::string& data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(data.data(), static_cast<std::streamsize>(data.size()));
}
}  // namespace

int main() {
  const auto source = std::filesystem::temp_directory_path() / "eqmdsk-gfile";
  const auto target = std::filesystem::temp_directory_path() / "eqmdsk-gfile-out";
  write_file(source, synthetic_gfile());
  eqmdsk::GFile file(source.string());
  assert(file.keys().size() == 26);
  assert(get<std::int64_t>(file, "NW") == 3);
  assert(get<eqmdsk::DoubleMatrix>(file, "PSIRZ")(1, 2) == 202.0);
  std::get<eqmdsk::DoubleMatrix>(file.at("PSIRZ"))(1, 2) = -99.0;
  file.write(target.string());
  eqmdsk::GFile reparsed(target.string());
  assert(get<eqmdsk::DoubleMatrix>(reparsed, "PSIRZ")(1, 2) == -99.0);

  const auto real = std::filesystem::path(EQMDSK_LOCAL_DATA_DIR) / "g067590.03300";
  if (std::filesystem::exists(real)) {
    eqmdsk::GFile fixture(real.string());
    assert(get<std::int64_t>(fixture, "NW") == 129);
    assert(get<std::int64_t>(fixture, "IPLCOUT") == 1);
    assert(get<eqmdsk::DoubleMatrix>(fixture, "PCURRT").rows() == 129);
    assert(fixture.aux_namelist() != nullptr);
    assert(fixture.aux_namelist()->contains("OUT1"));
    fixture.write(target.string());
    eqmdsk::GFile roundtrip(target.string());
    assert(roundtrip.keys() == fixture.keys());
    assert(get<eqmdsk::DoubleMatrix>(roundtrip, "PSIRZ").isApprox(
        get<eqmdsk::DoubleMatrix>(fixture, "PSIRZ"), 2e-9));
    assert(get<eqmdsk::DoubleMatrix>(roundtrip, "PCURRT").isApprox(
        get<eqmdsk::DoubleMatrix>(fixture, "PCURRT"), 2e-9));
    assert(roundtrip.aux_namelist() != nullptr);
    assert(roundtrip.aux_namelist()->keys() == fixture.aux_namelist()->keys());
  }
  std::error_code ignored;
  std::filesystem::remove(source, ignored);
  std::filesystem::remove(target, ignored);
}
