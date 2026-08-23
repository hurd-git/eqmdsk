#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "eqmdsk/eqmdsk.hpp"

namespace {

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
  output.push_back('\n');
}

std::string g_seed() {
  std::ostringstream header;
  header.imbue(std::locale::classic());
  header << std::left << std::setw(48) << "FUZZ SEED" << std::right
         << std::setw(4) << 0 << std::setw(4) << 2 << std::setw(4) << 2
         << '\n';
  auto output = header.str();
  append_reals(output, {1, 2, 3, 4, 5});
  append_reals(output, {6, 7, -0.5, 0.5, -2});
  append_reals(output, {4, -0.5, 0, 6, 0});
  append_reals(output, {7, 0, 0.5, 0, 0});
  append_reals(output, {1, 2});
  append_reals(output, {3, 4});
  append_reals(output, {5, 6});
  append_reals(output, {7, 8});
  append_reals(output, {9, 10, 11, 12});
  append_reals(output, {1, 2});
  output += "    0    0\n";
  return output;
}

std::string a_seed() {
  std::string output =
      " 01-Jan-00 00/00/0000\n"
      "      1               1\n";
  output += " " + input_real(1.0) + "\n";
  std::ostringstream control;
  control.imbue(std::locale::classic());
  control << '*' << std::fixed << std::setprecision(3) << std::setw(8) << 1.0
          << std::string(9, ' ') << std::setw(5) << 1
          << std::string(11, ' ') << std::setw(5) << 0 << " LIM"
          << std::setw(4) << 0 << std::setw(4) << 0 << " QMF"
          << std::setw(6) << 0 << std::setw(5) << 0 << '\n';
  output += control.str();
  for (int record = 0; record < 17; ++record) {
    append_reals(output,
                 {static_cast<double>(record + 1),
                  static_cast<double>(record + 2),
                  static_cast<double>(record + 3),
                  static_cast<double>(record + 4)});
  }
  output += "     0    0    0    0\n";
  return output;
}

std::vector<std::string> mutations(const std::string& seed) {
  std::vector<std::string> result;
  result.emplace_back();
  result.push_back(seed.substr(0, 1));
  result.push_back(seed.substr(0, seed.size() / 2));
  result.push_back(seed + std::string("\0\xffTRAIL", 8));
  const auto step = std::max<std::size_t>(1, seed.size() / 16);
  for (std::size_t position = 0; position < seed.size(); position += step) {
    auto changed = seed;
    changed[position] = static_cast<char>(
        static_cast<unsigned char>(changed[position]) ^ 0xffU);
    result.push_back(std::move(changed));
    result.push_back(seed.substr(0, position) + seed.substr(position + 1));
  }
  return result;
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("unable to create fuzz smoke input");
  }
}

template <typename File>
void exercise(const std::filesystem::path& directory, const std::string& name,
              const std::string& seed) {
  const auto input = directory / (name + ".input");
  const auto output = directory / (name + ".output");
  for (const auto& payload : mutations(seed)) {
    write_bytes(input, payload);
    try {
      const File parsed(input.string());
      try {
        parsed.save(output.string());
        const File reparsed(output.string());
        static_cast<void>(reparsed);
      } catch (const eqmdsk::Error&) {
      }
    } catch (const eqmdsk::Error&) {
    }
  }
  std::filesystem::remove(input);
  std::filesystem::remove(output);
}

}  // namespace

int main() {
  const auto directory = std::filesystem::temp_directory_path();
  exercise<eqmdsk::GFile>(directory, "eqmdsk-fuzz-smoke-g", g_seed());
  exercise<eqmdsk::AFile>(directory, "eqmdsk-fuzz-smoke-a", a_seed());
  constexpr char k_seed_bytes[] =
      "outside\0\n&IN\n A=1\n B=2*3.0D+00\n/\n";
  exercise<eqmdsk::KFile>(
      directory, "eqmdsk-fuzz-smoke-k",
      std::string(k_seed_bytes, sizeof(k_seed_bytes) - 1));
  exercise<eqmdsk::SFile>(directory, "eqmdsk-fuzz-smoke-s",
                          "x\ny\ntitle\n1 2 3 4\n");
}
