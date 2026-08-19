#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>

#include "eqmdsk/afile.hpp"
#include "eqmdsk/error.hpp"

namespace {

std::filesystem::path fixture_path() {
#ifdef EQMDSK_LOCAL_DATA_DIR
  return std::filesystem::path(EQMDSK_LOCAL_DATA_DIR) / "a067590.03300";
#else
  auto source = std::filesystem::absolute(std::filesystem::path(__FILE__));
  return source.parent_path().parent_path().parent_path().parent_path() /
         "data" / "a067590.03300";
#endif
}

bool same_field(const eqmdsk::FieldValue& left,
                const eqmdsk::FieldValue& right) {
  if (left.index() != right.index()) {
    return false;
  }
  return std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        const auto* other = std::get_if<T>(&right);
        if constexpr (std::is_same_v<T, double>) {
          return other != nullptr &&
                 std::abs(value - *other) <=
                     1e-9 * std::max(1.0, std::abs(value));
        } else if constexpr (std::is_same_v<T, eqmdsk::DoubleVector> ||
                             std::is_same_v<T, eqmdsk::DoubleMatrix>) {
          return other != nullptr && value.isApprox(*other, 1e-9);
        } else {
          return other != nullptr && value == *other;
        }
      },
      left);
}

bool close_value(double left, double right) {
  return std::abs(left - right) <=
         1e-9 * std::max(1.0, std::abs(right));
}

std::string input_real(double value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::uppercase << std::scientific << std::setprecision(9)
         << std::setw(16) << value;
  return output.str();
}

std::string synthetic_afile() {
  std::string result = " 01-Jan-00 00/00/0000\n";
  result += "      1               1\n";
  result += " " + input_real(1.0) + "\n";

  std::ostringstream control;
  control.imbue(std::locale::classic());
  control << '*' << std::fixed << std::setprecision(3) << std::setw(8) << 1.0
          << std::string(9, ' ') << std::setw(5) << 1
          << std::string(11, ' ') << std::setw(5) << 0 << ' ' << "LIM" << ' '
          << std::setw(3) << 3 << ' ' << std::setw(3) << 1 << ' ' << "QMF"
          << ' ' << std::setw(5) << 0 << std::setw(5) << 0 << '\n';
  result += control.str();

  for (int record = 0; record < 6; ++record) {
    result += input_real(static_cast<double>(record + 1));
    result += input_real(static_cast<double>(record + 2));
    result += input_real(static_cast<double>(record + 3));
    result += input_real(static_cast<double>(record + 4));
    result += '\n';
  }
  result += input_real(10.0) + input_real(11.0) + input_real(12.0) +
            input_real(999.0) + '\n';
  result += input_real(13.0) + input_real(14.0) + input_real(15.0) +
            input_real(999.0) + '\n';
  result += input_real(14.0) + '\n';
  result += input_real(15.0) + '\n';
  for (int record = 6; record < 17; ++record) {
    result += input_real(static_cast<double>(record + 1));
    result += input_real(static_cast<double>(record + 2));
    result += input_real(static_cast<double>(record + 3));
    result += input_real(static_cast<double>(record + 4));
    result += '\n';
  }
  result += "     1    1    0    0\n";
  result += input_real(21.0) + input_real(22.0) + input_real(999.0) +
            input_real(999.0) + '\n';
  return result;
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main() {
  const auto synthetic_path = std::filesystem::temp_directory_path() /
                              "eqmdsk-afile-synthetic.test";
  const auto synthetic_output = std::filesystem::temp_directory_path() /
                                "eqmdsk-afile-synthetic-output.test";
  write_bytes(synthetic_path, synthetic_afile());
  eqmdsk::AFile synthetic(synthetic_path);
  assert(std::get<std::int64_t>(synthetic.at("SHOT")) == 1);
  assert(std::get<eqmdsk::DoubleVector>(synthetic.at("RCO2V")).size() == 3);
  assert(std::get<eqmdsk::DoubleVector>(synthetic.at("RCO2V"))[2] == 12.0);
  assert(std::get<eqmdsk::DoubleVector>(synthetic.at("CSILOP"))[0] == 21.0);
  assert(std::get<eqmdsk::DoubleVector>(synthetic.at("CMPR2"))[0] == 22.0);
  assert(synthetic.optional_record_count() == 0);
  std::get<double>(synthetic.at("CHISQ")) = -1.0e100;
  synthetic.write(synthetic_output);
  std::ifstream formatted_input(synthetic_output, std::ios::binary);
  const std::string formatted((std::istreambuf_iterator<char>(formatted_input)),
                              std::istreambuf_iterator<char>());
  assert(formatted.find("-0.100000000+101") != std::string::npos);
  const eqmdsk::AFile synthetic_reparsed(synthetic_output);
  assert(close_value(std::get<double>(synthetic_reparsed.at("CHISQ")),
                     -1.0e100));

  auto damaged_time = synthetic_afile();
  const auto control_offset = damaged_time.find('*');
  assert(control_offset != std::string::npos);
  damaged_time.replace(control_offset + 1, 8, "BADTIME!");
  write_bytes(synthetic_path, damaged_time);
  const eqmdsk::AFile recovered_time(synthetic_path);
  assert(std::get<double>(recovered_time.at("TIME")) == 1.0);
  recovered_time.write(synthetic_output);
  assert(std::get<double>(eqmdsk::AFile(synthetic_output).at("TIME")) == 1.0);

  const auto third_header_begin = damaged_time.find('\n',
      damaged_time.find('\n') + 1) + 1;
  const auto third_header_end = damaged_time.find('\n', third_header_begin) + 1;
  damaged_time.erase(third_header_begin,
                     third_header_end - third_header_begin);
  write_bytes(synthetic_path, damaged_time);
  assert(std::get<double>(eqmdsk::AFile(synthetic_path).at("TIME")) == 0.0);
  std::filesystem::remove(synthetic_path);
  std::filesystem::remove(synthetic_output);

  const auto fixture = fixture_path();
  if (!std::filesystem::exists(fixture)) {
    return 0;  // The local compatibility fixture is intentionally not shipped.
  }

  const eqmdsk::AFile input(fixture);
  assert(input.format_name() == std::string("AFile"));
  assert(std::get<std::int64_t>(input.at("SHOT")) == 67590);
  assert(std::get<double>(input.at("TIME")) == 3300.0);
  assert(std::get<std::int64_t>(input.at("MCO2V")) == 3);
  assert(std::get<std::int64_t>(input.at("MCO2R")) == 2);
  assert(std::get<std::int64_t>(input.at("NSILOP0")) == 35);
  assert(std::get<std::int64_t>(input.at("MAGPRI0")) == 76);
  assert(std::get<std::int64_t>(input.at("NFCOIL0")) == 12);
  assert(std::get<std::int64_t>(input.at("NESUM0")) == 1);
  assert(std::get<eqmdsk::DoubleVector>(input.at("CSILOP")).size() == 35);
  assert(std::get<eqmdsk::DoubleVector>(input.at("CMPR2")).size() == 76);
  assert(std::get<eqmdsk::DoubleVector>(input.at("CCBRSP")).size() == 12);
  assert(std::get<eqmdsk::DoubleVector>(input.at("ECCURT")).size() == 1);
  assert(input.optional_record_count() == 14);
  assert(input.header().find("13-Oct-23") != std::string::npos);
  assert(input.footer().size() == 48);
  assert(input.footer().find('\0') != std::string::npos);
  assert(input.footer().find("MAG") != std::string::npos);

  auto output = std::filesystem::temp_directory_path() /
                "eqmdsk-afile-roundtrip.test";
  input.write(output);
  eqmdsk::AFile reparsed(output);
  assert(std::get<std::int64_t>(reparsed.at("SHOT")) == 67590);
  assert(std::abs(std::get<double>(reparsed.at("CHISQ")) - 4.87320554) <
         1e-9);
  assert(reparsed.optional_record_count() == 14);
  assert(reparsed.footer() == input.footer());
  assert(reparsed.keys() == input.keys());
  for (const auto& name : input.keys()) {
    assert(same_field(input.at(name), reparsed.at(name)));
  }

  std::get<double>(reparsed.at("CHISQ")) = 12.5;
  std::get<std::int64_t>(reparsed.at("SHOT")) = 67591;
  reparsed.write(output);
  const eqmdsk::AFile modified(output);
  assert(std::get<std::int64_t>(modified.at("SHOT")) == 67591);
  assert(std::get<double>(modified.at("CHISQ")) == 12.5);

  std::ifstream source(fixture, std::ios::binary);
  std::string truncated((std::istreambuf_iterator<char>(source)),
                        std::istreambuf_iterator<char>());
  truncated.resize(400);
  std::ofstream bad(output, std::ios::binary | std::ios::trunc);
  bad.write(truncated.data(), static_cast<std::streamsize>(truncated.size()));
  bad.close();
  bool rejected = false;
  try {
    static_cast<void>(eqmdsk::AFile(output));
  } catch (const eqmdsk::ParseError&) {
    rejected = true;
  }
  assert(rejected);
  std::filesystem::remove(output);
}
