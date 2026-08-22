#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "eqmdsk/afile.hpp"

namespace {
std::string real_record(double start) {
  std::string result = " ";
  for (int index = 0; index < 4; ++index) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%16.9E", start + index);
    result += buffer;
  }
  return result + "\n";
}

std::string synthetic_afile() {
  std::string output = " 01-Jan-00 00/00/0000\n      1               1\n";
  output += " " + std::string(" 1.000000000E+00") + "\n";
  output += "*   1.000       1           0 SNT   3   2 CLC       0    0\n";
  for (int record = 0; record < 6; ++record) output += real_record(record + 1);
  output += " 1.000000000E+01 1.100000000E+01 1.200000000E+01 9.990000000E+02\n";
  output += " 1.300000000E+01 1.400000000E+01 1.500000000E+01 9.990000000E+02\n";
  output += real_record(14) + real_record(16);
  for (int record = 6; record < 17; ++record) output += real_record(record + 1);
  output += "     1    1    0    0\n";
  output += " 2.100000000E+01 2.200000000E+01 9.990000000E+02 9.990000000E+02\n";
  output += "producer footer\n";
  return output;
}

void write_file(const std::filesystem::path& path, const std::string& data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(data.data(), static_cast<std::streamsize>(data.size()));
}
}  // namespace

int main() {
  const auto source = std::filesystem::temp_directory_path() / "eqmdsk-afile";
  const auto target = std::filesystem::temp_directory_path() / "eqmdsk-afile-out";
  write_file(source, synthetic_afile());
  eqmdsk::AFile file(source.string());
  assert(file.keys().size() > 40);
  assert(std::get<std::int64_t>(file.at("SHOT")) == 1);
  assert(std::get<eqmdsk::DoubleVector>(file.at("RCO2V")).size() == 3);
  assert(file.header().find("01-Jan-00") != std::string::npos);
  assert(file.footer() == "producer footer\n");
  auto edited_header = file.header();
  edited_header.replace(1, 9, "02-Feb-00");
  file.set_header(std::move(edited_header));
  file.set_footer("edited footer\n");
  std::get<double>(file.at("CHISQ")) = -1.0e100;
  file.save(target.string());
  eqmdsk::AFile reparsed(target.string());
  assert(std::abs(std::get<double>(reparsed.at("CHISQ")) + 1.0e100) < 1e90);
  assert(reparsed.header().find("02-Feb-00") != std::string::npos);
  assert(reparsed.footer() == "edited footer\n");

  const auto real = std::filesystem::path(EQMDSK_LOCAL_DATA_DIR) / "a067590.03300";
  if (std::filesystem::exists(real)) {
    eqmdsk::AFile fixture(real.string());
    fixture.save(target.string());
    eqmdsk::AFile roundtrip(target.string());
    assert(roundtrip.keys() == fixture.keys());
    for (const auto& name : fixture.keys()) {
      assert(roundtrip.contains(name));
    }
  }
  std::error_code ignored;
  std::filesystem::remove(source, ignored);
  std::filesystem::remove(target, ignored);
}
