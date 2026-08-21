#include "detail/fortran.hpp"

#include <Eigen/Core>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
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

bool has_nonzero_significand(std::string_view token) noexcept {
  const auto exponent = token.find_first_of("Ee");
  const auto end = exponent == std::string_view::npos ? token.size() : exponent;
  for (std::size_t index = 0; index < end; ++index) {
    if (token[index] >= '1' && token[index] <= '9') {
      return true;
    }
  }
  return false;
}

void append_utf8_replacement(std::string& output) {
  output.append("\xEF\xBF\xBD", 3);
}

std::string sanitize_utf8(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  std::size_t position = 0;
  while (position < input.size()) {
    const auto lead = static_cast<unsigned char>(input[position]);
    if (lead <= 0x7f) {
      output.push_back(input[position++]);
      continue;
    }

    std::size_t length = 0;
    if (lead >= 0xc2 && lead <= 0xdf) {
      length = 2;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      length = 3;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      length = 4;
    }

    bool valid = length != 0 && position + length <= input.size();
    for (std::size_t index = 1; valid && index < length; ++index) {
      const auto continuation =
          static_cast<unsigned char>(input[position + index]);
      valid = continuation >= 0x80 && continuation <= 0xbf;
    }
    if (valid && length == 3) {
      const auto second = static_cast<unsigned char>(input[position + 1]);
      valid = (lead != 0xe0 || second >= 0xa0) &&
              (lead != 0xed || second <= 0x9f);
    } else if (valid && length == 4) {
      const auto second = static_cast<unsigned char>(input[position + 1]);
      valid = (lead != 0xf0 || second >= 0x90) &&
              (lead != 0xf4 || second <= 0x8f);
    }

    if (valid) {
      output.append(input.data() + position, length);
      position += length;
    } else {
      append_utf8_replacement(output);
      ++position;
    }
  }
  return output;
}

}  // namespace

std::string path_for_diagnostic(const std::filesystem::path& path) {
  try {
    const auto encoded = path.u8string();
    const auto* bytes = reinterpret_cast<const char*>(encoded.data());
    return sanitize_utf8(std::string_view(bytes, encoded.size()));
  } catch (...) {
    return "<path encoding unavailable>";
  }
}

std::filesystem::path path_from_string(const std::string& path) {
#ifdef _WIN32
  return std::filesystem::u8path(path);
#else
  return std::filesystem::path(path);
#endif
}

std::string path_for_diagnostic(const std::string& path) {
  return path_for_diagnostic(path_from_string(path));
}

std::string read_binary_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw IOError("unable to open file for reading: " +
                  path_for_diagnostic(path));
  }
  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  if (end < 0) {
    throw IOError("unable to determine file size: " +
                  path_for_diagnostic(path));
  }
  const auto length = static_cast<std::uintmax_t>(
      static_cast<std::streamoff>(end));
  if (length > std::numeric_limits<std::size_t>::max() ||
      length > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::streamsize>::max())) {
    throw IOError("file is too large to read into memory: " +
                  path_for_diagnostic(path));
  }
  std::string bytes(static_cast<std::size_t>(length), '\0');
  input.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
      throw IOError("file changed or ended before the advertised size was read: " +
                    path_for_diagnostic(path));
    }
  }
  if (input.bad()) {
    throw IOError("unable to read file: " + path_for_diagnostic(path));
  }
  return bytes;
}

std::string read_binary_file(const std::string& path) {
  return read_binary_file(path_from_string(path));
}

void write_binary_file(const std::filesystem::path& path,
                       const std::string& bytes) {
  if (bytes.size() > static_cast<std::size_t>(
                         std::numeric_limits<std::streamsize>::max())) {
    throw IOError("file is too large to write: " + path_for_diagnostic(path));
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw IOError("unable to open file for writing: " +
                  path_for_diagnostic(path));
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw IOError("unable to write file: " + path_for_diagnostic(path));
  }
  output.close();
  if (!output) {
    throw IOError("unable to finish writing file: " +
                  path_for_diagnostic(path));
  }
}

void write_binary_file(const std::string& path, const std::string& bytes) {
  write_binary_file(path_from_string(path), bytes);
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

bool parse_fortran_real(std::string_view text, double& value) {
  auto token = trim_copy(text);
  if (token.empty()) {
    return false;
  }
  for (char& character : token) {
    if (character == 'D' || character == 'd') {
      character = 'E';
    }
  }
  const auto decimal = token.find('.');
  const auto exponent = token.find_first_of("Ee");
  if (decimal != std::string::npos && exponent == std::string::npos) {
    const auto sign = token.find_first_of("+-", decimal + 1);
    if (sign != std::string::npos) {
      token.insert(sign, "E");
    }
  }
  if (!token.empty() && token.front() == '+') {
    token.erase(token.begin());
  }
  if (token.empty()) {
    return false;
  }
  std::istringstream stream(token);
  stream.imbue(std::locale::classic());
  stream >> std::noskipws >> value;
  if (!stream) {
    // libc++ reports ERANGE through failbit even when strtod produced an exact,
    // representable subnormal. Accept that value only when the complete token
    // was consumed; zero underflow and overflow remain invalid.
    return stream.eof() && std::fpclassify(value) == FP_SUBNORMAL;
  }
  if (value == 0.0 && has_nonzero_significand(token)) {
    return false;
  }
  return stream.peek() == std::char_traits<char>::eof() &&
         std::isfinite(value);
}

std::string format_e16_9(double value) {
  if (!std::isfinite(value)) {
    throw ValidationError("cannot write a non-finite floating-point value");
  }

  std::ostringstream normalized_stream;
  normalized_stream.imbue(std::locale::classic());
  normalized_stream << std::uppercase << std::scientific
                    << std::setprecision(8) << value;
  const auto normalized = normalized_stream.str();
  const auto exponent_marker = normalized.find('E');
  const auto decimal = normalized.find('.');
  const auto first_digit = normalized.find_first_of("0123456789");
  if (exponent_marker == std::string::npos || decimal == std::string::npos ||
      first_digit == std::string::npos || decimal <= first_digit ||
      exponent_marker <= decimal) {
    throw ValidationError("unable to format floating-point value as E16.9");
  }

  std::string digits;
  digits.reserve(9);
  digits.push_back(normalized[first_digit]);
  digits.append(normalized, decimal + 1,
                std::min<std::size_t>(8, exponent_marker - decimal - 1));
  digits.append(9 - digits.size(), '0');

  int exponent = 0;
  try {
    exponent = std::stoi(normalized.substr(exponent_marker + 1));
  } catch (...) {
    throw ValidationError("unable to format floating-point exponent as E16.9");
  }
  if (value != 0.0) {
    ++exponent;
  }
  const auto exponent_magnitude = std::abs(exponent);
  if (exponent_magnitude > 999) {
    throw ValidationError("floating-point exponent does not fit E16.9");
  }

  std::ostringstream suffix;
  suffix.imbue(std::locale::classic());
  if (exponent_magnitude <= 99) {
    suffix << 'E' << (exponent < 0 ? '-' : '+') << std::setfill('0')
           << std::setw(2) << exponent_magnitude;
  } else {
    suffix << (exponent < 0 ? '-' : '+') << std::setfill('0')
           << std::setw(3) << exponent_magnitude;
  }

  std::string result;
  result.reserve(16);
  result.push_back(std::signbit(value) ? '-' : ' ');
  result += "0.";
  result += digits;
  result += suffix.str();
  if (result.size() != 16) {
    throw ValidationError("floating-point value does not fit E16.9");
  }
  return result;
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
  double value = 0.0;
  if (!parse_fortran_real(token, value)) {
    fail("invalid floating-point value", field);
  }
  return value;
}

std::int64_t NumericCursor::next_integer(const std::string& field) {
  auto token = next_number_token(field, true);
  if (!token.empty() && token.front() == '+') {
    token.erase(token.begin());
  }
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
  output_ += format_e16_9(number);
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
  const auto product = left * right;
  if (static_cast<std::uintmax_t>(product) >
      static_cast<std::uintmax_t>(
          std::numeric_limits<Eigen::Index>::max())) {
    throw ValidationError(description + " size exceeds the Eigen index range");
  }
  return product;
}

std::size_t checked_count(std::int64_t value, const std::string& field) {
  if (value < 0) {
    throw ValidationError(field + " must not be negative");
  }
  const auto unsigned_value = static_cast<std::uint64_t>(value);
  if (unsigned_value >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw ValidationError(field + " exceeds the platform size_t range");
  }
  if (unsigned_value > static_cast<std::uint64_t>(
                           std::numeric_limits<Eigen::Index>::max())) {
    throw ValidationError(field + " exceeds the Eigen index range");
  }
  return static_cast<std::size_t>(value);
}

}  // namespace eqmdsk::detail
