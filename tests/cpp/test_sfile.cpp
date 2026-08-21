#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "eqmdsk/sfile.hpp"

namespace {
void write_file(const std::filesystem::path& path, const std::string& data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(data.data(), static_cast<std::streamsize>(data.size()));
}
}

int main() {
  const auto source = std::filesystem::temp_directory_path() / "eqmdsk-sfile";
  const auto target = std::filesystem::temp_directory_path() / "eqmdsk-sfile-out";
  write_file(source, "R\nZ\nTEST\n1 10 0.1 1\n2 20 0.2 2\n");
  eqmdsk::SFile file(source.string());
  assert(file.keys().size() == 7);
  assert(std::get<std::string>(file.at("TITLE")) == "TEST");
  assert(std::get<eqmdsk::DoubleVector>(file.at("X"))[1] == 2.0);
  std::get<eqmdsk::DoubleVector>(file.at("Y"))[0] = 11.0;
  file.write(target.string());
  eqmdsk::SFile reparsed(target.string());
  assert(std::get<eqmdsk::DoubleVector>(reparsed.at("Y"))[0] == 11.0);
  std::error_code ignored;
  std::filesystem::remove(source, ignored);
  std::filesystem::remove(target, ignored);
}
