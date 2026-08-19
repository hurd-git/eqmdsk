#include "detail/fortran.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

#include "eqmdsk/error.hpp"

namespace eqmdsk::detail {
namespace {

std::pair<std::size_t, std::size_t> line_and_column(const std::string& input,
                                                    std::size_t position) {
  std::size_t line = 1;
  std::size_t column = 1;
  const auto end = std::min(position, input.size());
  for (std::size_t index = 0; index < end; ++index) {
    if (input[index] == '\n') {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  }
  return {line, column};
}

bool is_digit(char value) noexcept { return value >= '0' && value <= '9'; }

}  // namespace

std::string read_binary_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw IOError("unable to open file for reading: " + path.string());
  }
  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  if (end < 0) {
    throw IOError("unable to determine file size: " + path.string());
  }
  std::string bytes(static_cast<std::size_t>(end), '\0');
  input.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  if (!input && !input.eof()) {
    throw IOError("unable to read file: " + path.string());
  }
  return bytes;
}

void write_binary_file(const std::filesystem::path& path,
                       const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw IOError("unable to open file for writing: " + path.string());
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw IOError("unable to write file: " + path.string());
  }
}

std::string trim_copy(std::string_view value) {
  std::size_t first = 0;
  while (first < value.size() &&
         (value[first] == ' ' || value[first] == '\t' || value[first] == '\r' ||
          value[first] == '\n')) {
    ++first;
  }
  std::size_t last = value.size();
  while (last > first &&
         (value[last - 1] == ' ' || value[last - 1] == '\t' ||
          value[last - 1] == '\r' || value[last - 1] == '\n')) {
    --last;
  }
  return std::string(value.substr(first, last - first));
}

std::string rtrim_copy(std::string_view value) {
  std::size_t last = value.size();
  while (last > 0 &&
         (value[last - 1] == ' ' || value[last - 1] == '\t' ||
          value[last - 1] == '\r' || value[last - 1] == '\n')) {
    --last;
  }
  return std::string(value.substr(0, last));
}

NumericCursor::NumericCursor(const std::string& input, std::size_t position,
                             std::string filename)
    : input_(input), position_(position), filename_(std::move(filename)) {}

void NumericCursor::skip_space() noexcept {
  while (position_ < input_.size()) {
    const char value = input_[position_];
    if (value != ' ' && value != '\t' && value != '\r' && value != '\n' &&
        value != ',') {
      break;
    }
    ++position_;
  }
}

bool NumericCursor::has_nonspace() const noexcept {
  auto position = position_;
  while (position < input_.size()) {
    const char value = input_[position];
    if (value != ' ' && value != '\t' && value != '\r' && value != '\n' &&
        value != ',') {
      return true;
    }
    ++position;
  }
  return false;
}

std::size_t NumericCursor::position_after_line_ending() const noexcept {
  auto position = position_;
  while (position < input_.size() &&
         (input_[position] == ' ' || input_[position] == '\t')) {
    ++position;
  }
  if (position < input_.size() && input_[position] == '\r') {
    ++position;
  }
  if (position < input_.size() && input_[position] == '\n') {
    ++position;
  }
  return position;
}

[[noreturn]] void NumericCursor::fail(const std::string& message,
                                      const std::string& field) const {
  const auto [line, column] = line_and_column(input_, position_);
  const auto context = field.empty() ? message : message + " while reading " + field;
  throw ParseError(context, filename_, line, column);
}

std::string NumericCursor::next_number_token(const std::string& field,
                                             bool integer) {
  skip_space();
  if (position_ >= input_.size()) {
    fail("unexpected end of file", field);
  }
  const auto start = position_;
  if (input_[position_] == '+' || input_[position_] == '-') {
    ++position_;
  }
  const auto before_digits = position_;
  while (position_ < input_.size() && is_digit(input_[position_])) {
    ++position_;
  }
  bool have_digits = position_ != before_digits;
  bool have_decimal = false;
  if (!integer && position_ < input_.size() && input_[position_] == '.') {
    have_decimal = true;
    ++position_;
    const auto fractional_start = position_;
    while (position_ < input_.size() && is_digit(input_[position_])) {
      ++position_;
    }
    have_digits = have_digits || position_ != fractional_start;
  }
  if (!have_digits) {
    position_ = start;
    fail("expected a numeric value", field);
  }
  if (!integer && position_ < input_.size() &&
      (input_[position_] == 'E' || input_[position_] == 'e' ||
       input_[position_] == 'D' || input_[position_] == 'd')) {
    ++position_;
    if (position_ < input_.size() &&
        (input_[position_] == '+' || input_[position_] == '-')) {
      ++position_;
    }
    const auto exponent_start = position_;
    while (position_ < input_.size() && is_digit(input_[position_])) {
      ++position_;
    }
    if (position_ == exponent_start) {
      position_ = start;
      fail("malformed exponent", field);
    }
  } else if (!integer && have_decimal && position_ < input_.size() &&
             (input_[position_] == '+' || input_[position_] == '-')) {
    // Some Fortran writers omit the exponent marker for large exponents.
    const auto exponent_sign = position_;
    auto probe = position_ + 1;
    while (probe < input_.size() && is_digit(input_[probe]) &&
           probe - exponent_sign <= 4) {
      ++probe;
    }
    const auto digits = probe - exponent_sign - 1;
    if (digits >= 2 && digits <= 3) {
      position_ = probe;
    }
  }
  return input_.substr(start, position_ - start);
}

double NumericCursor::next_real(const std::string& field) {
  auto token = next_number_token(field, false);
  for (char& value : token) {
    if (value == 'D' || value == 'd') {
      value = 'E';
    }
  }
  // Restore an omitted E before a trailing signed exponent.
  const auto decimal = token.find('.');
  const auto exponent = token.find_first_of("Ee", decimal == std::string::npos
                                                       ? 0
                                                       : decimal);
  if (decimal != std::string::npos && exponent == std::string::npos) {
    const auto sign = token.find_first_of("+-", decimal + 1);
    if (sign != std::string::npos) {
      token.insert(sign, "E");
    }
  }
  errno = 0;
  char* end = nullptr;
  const double value = std::strtod(token.c_str(), &end);
  if (end != token.c_str() + token.size() || errno == ERANGE) {
    fail("invalid floating-point value", field);
  }
  return value;
}

std::int64_t NumericCursor::next_integer(const std::string& field) {
  const auto token = next_number_token(field, true);
  std::int64_t value = 0;
  const auto result =
      std::from_chars(token.data(), token.data() + token.size(), value);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
    fail("invalid integer value", field);
  }
  return value;
}

std::vector<double> NumericCursor::real_array(std::size_t count,
                                              const std::string& field) {
  std::vector<double> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(next_real(field + "[" + std::to_string(index) + "]"));
  }
  return result;
}

void FortranRealWriter::value(double number) {
  if (!std::isfinite(number)) {
    throw ValidationError("cannot write a non-finite floating-point value");
  }
  std::ostringstream field;
  field.imbue(std::locale::classic());
  field << std::uppercase << std::scientific << std::setprecision(9)
        << std::setw(16) << number;
  const auto text = field.str();
  if (text.size() > 16) {
    throw ValidationError("floating-point value does not fit E16.9");
  }
  output_ += text;
  ++column_;
  if (column_ == 5) {
    output_ += '\n';
    column_ = 0;
  }
}

void FortranRealWriter::finish_line() {
  if (column_ != 0) {
    output_ += '\n';
    column_ = 0;
  }
}

std::size_t checked_product(std::size_t left, std::size_t right,
                            const std::string& description) {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
    throw ValidationError(description + " size overflows size_t");
  }
  return left * right;
}

std::size_t checked_count(std::int64_t value, const std::string& field) {
  if (value < 0) {
    throw ValidationError(field + " must not be negative");
  }
  return static_cast<std::size_t>(value);
}

}  // namespace eqmdsk::detail

