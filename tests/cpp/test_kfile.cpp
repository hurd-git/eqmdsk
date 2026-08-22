#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "eqmdsk/kfile.hpp"

namespace {
void write_file(const std::filesystem::path& path, const std::string& data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(data.data(), static_cast<std::streamsize>(data.size()));
}
}

int main(int argc, char** argv) {
  const auto source = std::filesystem::temp_directory_path() / "eqmdsk-kfile";
  const auto target = std::filesystem::temp_directory_path() / "eqmdsk-kfile-out";
  write_file(source, "outside\n&IN1\n LIMITR = 60\n BTOR = -2.25\n FLAG = .true.\n/\n");
  eqmdsk::KFile file(source.string());
  assert(file.keys() == std::vector<std::string>{"IN1"});
  assert(file.contains("in1"));
  assert(std::get<std::int64_t>(file["IN1"]["LIMITR"]) == 60);
  assert(std::get<double>(file["IN1"]["BTOR"]) == -2.25);
  assert(std::get<bool>(file["IN1"]["FLAG"]));
  std::get<std::int64_t>(file["IN1"]["LIMITR"]) = 61;
  file.save(target.string());
  eqmdsk::KFile reparsed(target.string());
  assert(std::get<std::int64_t>(reparsed["IN1"]["LIMITR"]) == 61);

  if (argc > 1 && std::filesystem::exists(argv[1])) {
    eqmdsk::KFile real(argv[1]);
    assert(real.contains("IN1"));
    assert(std::get<std::int64_t>(real["IN1"]["LIMITR"]) == 60);
    real.save(target.string());
    eqmdsk::KFile roundtrip(target.string());
    assert(roundtrip.contains("IN1"));
    assert(std::get<std::int64_t>(roundtrip["IN1"]["LIMITR"]) == 60);
  }

  std::error_code ignored;
  std::filesystem::remove(source, ignored);
  std::filesystem::remove(target, ignored);
}
