#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "eqmdsk/error.hpp"
#include "eqmdsk/field.hpp"
#include "eqmdsk/sfile.hpp"

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto stamp = std::chrono::high_resolution_clock::now()
                           .time_since_epoch()
                           .count();
    path_ = std::filesystem::temp_directory_path() /
            ("eqmdsk-sfile-test-" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  assert(output.good());
}

std::string read_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

const eqmdsk::DoubleVector& vector(const eqmdsk::SFile& file,
                                   const char* name) {
  return std::get<eqmdsk::DoubleVector>(file.at(name));
}

eqmdsk::DoubleVector& vector(eqmdsk::SFile& file, const char* name) {
  return std::get<eqmdsk::DoubleVector>(file.at(name));
}

template <typename Exception, typename Function>
void assert_throws(Function&& function) {
  bool threw = false;
  try {
    function();
  } catch (const Exception&) {
    threw = true;
  }
  assert(threw);
}

void test_without_titles(const std::filesystem::path& directory) {
  const auto path = directory / "s-no-title";
  write_bytes(path, "1 2 0.1 0.2\n3 4 0.3 0.4\n");
  const eqmdsk::SFile file(path);

  assert(std::string(file.format_name()) == "SFile");
  assert(!file.fields().contains("XLABEL"));
  assert(!file.fields().contains("YLABEL"));
  assert(!file.fields().contains("TITLE"));
  assert(vector(file, "X").size() == 2);
  assert(vector(file, "X")[0] == 1.0);
  assert(vector(file, "Y")[1] == 4.0);
  assert(vector(file, "DX")[1] == 0.3);
  assert(vector(file, "DY")[0] == 0.2);
  assert(file.raw_sections().empty());
}

void test_three_titles_and_fortran_exponents(
    const std::filesystem::path& directory) {
  const auto path = directory / "s-three-title";
  write_bytes(path,
              "major radius\r\npressure\r\nFIT TITLE\r\n"
              "1.0D+00 2.0d+00 3.0E-1 4.0e-1\r\n");
  const eqmdsk::SFile file(path);

  assert(std::get<std::string>(file.at("XLABEL")) == "major radius");
  assert(std::get<std::string>(file.at("YLABEL")) == "pressure");
  assert(std::get<std::string>(file.at("TITLE")) == "FIT TITLE");
  assert(vector(file, "X")[0] == 1.0);
  assert(vector(file, "DY")[0] == 0.4);
}

void test_interstitial_text_and_roundtrip(
    const std::filesystem::path& directory) {
  const auto source = directory / "s-extra-source";
  const auto target = directory / "s-extra-target";
  std::string source_bytes =
      "x label\ny label\nfit title\n"
      "1 10 0.1 1\n"
      "COMMENT BETWEEN\r\n"
      "2 20 0.2 2\n";
  constexpr char opaque_tail[] = "COMMENT AFTER\0OPAQUE";
  source_bytes.append(opaque_tail, sizeof(opaque_tail) - 1);
  write_bytes(source, source_bytes);

  const eqmdsk::SFile original(source);
  assert(original.raw_sections().size() == 2);
  assert(original.raw_sections()[0].name == "extra_text");
  assert(original.raw_sections()[0].data == "COMMENT BETWEEN\r\n");
  assert(original.raw_sections()[1].data ==
         std::string(opaque_tail, sizeof(opaque_tail) - 1));
  original.write(target);

  const auto output = read_bytes(target);
  const auto first_row = output.find("1 10");
  const auto between = output.find("COMMENT BETWEEN");
  const auto second_row = output.find("2 20");
  const auto after = output.find("COMMENT AFTER");
  assert(first_row < between && between < second_row && second_row < after);

  const eqmdsk::SFile reparsed(target);
  assert(reparsed.raw_sections().size() == 2);
  assert(reparsed.raw_sections()[0].data == "COMMENT BETWEEN\r\n");
  assert(reparsed.raw_sections()[1].data ==
         std::string(opaque_tail, sizeof(opaque_tail) - 1));
  assert(vector(reparsed, "X")[1] == 2.0);
  assert(vector(reparsed, "Y")[0] == 10.0);
}

void test_bad_columns(const std::filesystem::path& directory) {
  const auto three = directory / "s-three-columns";
  const auto five = directory / "s-five-columns";
  const auto mixed = directory / "s-invalid-column";
  write_bytes(three, "1 2 3\n");
  write_bytes(five, "1 2 3 4 5\n");
  write_bytes(mixed, "1 2 broken 4\n");

  assert_throws<eqmdsk::ParseError>([&] { eqmdsk::SFile file(three); });
  assert_throws<eqmdsk::ParseError>([&] { eqmdsk::SFile file(five); });
  assert_throws<eqmdsk::ParseError>([&] { eqmdsk::SFile file(mixed); });
}

void test_precision_modification_and_default_write(
    const std::filesystem::path& directory) {
  const auto path = directory / "s-precision";
  write_bytes(path, "0 0 0 0\n");
  eqmdsk::SFile file(path);

  const double values[]{
      std::nextafter(1.0, 2.0),
      -std::numeric_limits<double>::min(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::denorm_min(),
  };
  vector(file, "X")[0] = values[0];
  vector(file, "Y")[0] = values[1];
  vector(file, "DX")[0] = values[2];
  vector(file, "DY")[0] = values[3];
  file.write();

  eqmdsk::SFile reparsed(path);
  assert(vector(reparsed, "X")[0] == values[0]);
  assert(vector(reparsed, "Y")[0] == values[1]);
  assert(vector(reparsed, "DX")[0] == values[2]);
  assert(vector(reparsed, "DY")[0] == values[3]);

  vector(reparsed, "Y")[0] = 42.25;
  const auto modified = directory / "s-modified";
  reparsed.write(modified);
  assert(vector(eqmdsk::SFile(modified), "Y")[0] == 42.25);
}

void test_write_validation(const std::filesystem::path& directory) {
  const auto source = directory / "s-validation";
  const auto target = directory / "s-validation-output";
  write_bytes(source, "1 2 3 4\n");

  {
    eqmdsk::SFile file(source);
    vector(file, "DX").conservativeResize(2);
    assert_throws<eqmdsk::ValidationError>([&] { file.write(target); });
  }
  {
    eqmdsk::SFile file(source);
    vector(file, "DY")[0] = std::numeric_limits<double>::infinity();
    assert_throws<eqmdsk::ValidationError>([&] { file.write(target); });
  }
  {
    eqmdsk::SFile file(source);
    file.fields().set("X", 1.0);
    assert_throws<eqmdsk::ValidationError>([&] { file.write(target); });
  }
}

}  // namespace

int main() {
  const TemporaryDirectory temporary;
  test_without_titles(temporary.path());
  test_three_titles_and_fortran_exponents(temporary.path());
  test_interstitial_text_and_roundtrip(temporary.path());
  test_bad_columns(temporary.path());
  test_precision_modification_and_default_write(temporary.path());
  test_write_validation(temporary.path());
}
