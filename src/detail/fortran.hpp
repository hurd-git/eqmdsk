#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace eqmdsk::detail {

// Return a valid UTF-8 representation suitable for diagnostics on every
// supported platform. Invalid byte sequences are replaced rather than passed
// into language bindings unchanged.
std::string path_for_diagnostic(const std::filesystem::path& path);

std::string read_binary_file(const std::filesystem::path& path);
void write_binary_file(const std::filesystem::path& path,
                       const std::string& bytes);

std::string trim_copy(std::string_view value);
std::string rtrim_copy(std::string_view value);
bool parse_fortran_real(std::string_view text, double& value);

// Format one real using the EFIT/Fortran E16.9 convention. Exponents whose
// magnitude needs three digits omit the `E` marker, as required to retain the
// fixed 16-character field width (for example, -0.100000000+101).
std::string format_e16_9(double value);

class NumericCursor {
 public:
  NumericCursor(const std::string& input, std::size_t position,
                std::string filename);

  double next_real(const std::string& field);
  std::int64_t next_integer(const std::string& field);
  std::vector<double> real_array(std::size_t count, const std::string& field);

  std::size_t position() const noexcept { return position_; }
  bool has_nonspace() const noexcept;
  std::size_t position_after_line_ending() const noexcept;

 private:
  void skip_space() noexcept;
  [[noreturn]] void fail(const std::string& message,
                         const std::string& field) const;
  std::string next_number_token(const std::string& field, bool integer);

  const std::string& input_;
  std::size_t position_;
  std::string filename_;
};

class FortranRealWriter {
 public:
  explicit FortranRealWriter(std::string& output) : output_(output) {}

  void value(double number);
  template <typename Range>
  void values(const Range& range) {
    for (const auto& number : range) {
      value(static_cast<double>(number));
    }
  }
  void finish_line();

 private:
  std::string& output_;
  int column_ = 0;
};

std::size_t checked_product(std::size_t left, std::size_t right,
                            const std::string& description);
std::size_t checked_count(std::int64_t value, const std::string& field);

}  // namespace eqmdsk::detail
