#include "detail/fortran.hpp"

#include <Eigen/Core>

#include <algorithm>
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
  auto first = text.begin();
  auto last = text.end();
  while (first != last && (*first == ' ' || *first == '\t' ||
                           *first == '\r' || *first == '\n')) {
    ++first;
  }
  while (last != first && (*(last - 1) == ' ' || *(last - 1) == '\t' ||
                           *(last - 1) == '\r' || *(last - 1) == '\n')) {
    --last;
  }
  if (first == last) {
    return false;
  }
  auto token_view = std::string_view(&*first,
                                     static_cast<std::size_t>(last - first));
  // Fortran D exponents and omitted exponent markers use the compatibility
  // path below. Standard floating-point charconv, when available, handles the
  // ordinary E/e form without allocating or constructing a stream.
#if EQMDSK_HAS_FLOAT_CHARCONV
  const auto decimal = token_view.find('.');
  const auto exponent = token_view.find_first_of("Ee");
  const auto has_d_exponent = token_view.find_first_of("Dd") !=
                              std::string_view::npos;
  const auto omitted_exponent =
      decimal != std::string_view::npos && exponent == std::string_view::npos &&
      token_view.find_first_of("+-", decimal + 1) != std::string_view::npos;
  if (!has_d_exponent && !omitted_exponent) {
    auto number = token_view;
    if (!number.empty() && number.front() == '+') {
      number.remove_prefix(1);
    }
    if (!number.empty()) {
      double parsed = 0.0;
      const auto result = std::from_chars(
          number.data(), number.data() + number.size(), parsed,
          std::chars_format::general);
      if (result.ec == std::errc{} &&
          result.ptr == number.data() + number.size() &&
          std::isfinite(parsed) &&
          !(parsed == 0.0 && has_nonzero_significand(number))) {
        value = parsed;
        return true;
      }
    }
  }
#endif
  auto token = std::string(token_view);
  for (char& character : token) {
    if (character == 'D' || character == 'd') {
      character = 'E';
    }
  }
  const auto normalized_decimal = token.find('.');
  const auto normalized_exponent = token.find_first_of("Ee");
  if (normalized_decimal != std::string::npos &&
      normalized_exponent == std::string::npos) {
    const auto sign = token.find_first_of("+-", normalized_decimal + 1);
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

void append_e16_9(std::string& output, double value) {
  if (!std::isfinite(value)) {
    throw ValidationError("cannot write a non-finite floating-point value");
  }

#if EQMDSK_HAS_FLOAT_CHARCONV
  char normalized[32]{};
  const auto converted = std::to_chars(
      normalized, normalized + sizeof(normalized), value,
      std::chars_format::scientific, 8);
  if (converted.ec != std::errc{} || converted.ptr == normalized ||
      converted.ptr >= normalized + sizeof(normalized)) {
    throw ValidationError("unable to format floating-point value as E16.9");
  }
  const auto* begin = normalized;
  const auto* end = converted.ptr;
  const bool negative = *begin == '-';
  if (negative) {
    ++begin;
  }
  const auto* decimal = std::find(begin, end, '.');
  const auto* exponent_marker = std::find(begin, end, 'e');
  if (decimal == end || exponent_marker == end || decimal >= exponent_marker ||
      decimal == begin || exponent_marker + 1 >= end) {
    throw ValidationError("unable to format floating-point exponent as E16.9");
  }
#else
  std::ostringstream normalized_stream;
  normalized_stream.imbue(std::locale::classic());
  normalized_stream << std::uppercase << std::scientific
                    << std::setprecision(8) << value;
  const auto normalized = normalized_stream.str();
  const auto* begin = normalized.data();
  const auto* end = begin + normalized.size();
  const auto* decimal = std::find(begin, end, '.');
  const auto* exponent_marker = std::find(begin, end, 'E');
  if (!normalized_stream || decimal == end || exponent_marker == end ||
      decimal >= exponent_marker || decimal == begin ||
      exponent_marker + 1 >= end) {
    throw ValidationError("unable to format floating-point value as E16.9");
  }

  const bool negative = std::signbit(value);
  if (negative) {
    ++begin;
  }
#endif

  int exponent = 0;
  const auto* exponent_digit = exponent_marker + 1;
  bool exponent_negative = false;
  if (*exponent_digit == '+' || *exponent_digit == '-') {
    exponent_negative = *exponent_digit == '-';
    ++exponent_digit;
  }
  if (exponent_digit == end) {
    throw ValidationError("unable to format floating-point exponent as E16.9");
  }
  for (; exponent_digit < end; ++exponent_digit) {
    if (*exponent_digit < '0' || *exponent_digit > '9' || exponent > 9999) {
      throw ValidationError("unable to format floating-point exponent as E16.9");
    }
    exponent = exponent * 10 + (*exponent_digit - '0');
  }
  if (exponent_negative) {
    exponent = -exponent;
  }
  if (value != 0.0) {
    ++exponent;
  }
  const auto exponent_magnitude = std::abs(exponent);
  if (exponent_magnitude > 999) {
    throw ValidationError("floating-point exponent does not fit E16.9");
  }

  char formatted[16]{};
  formatted[0] = negative ? '-' : ' ';
  formatted[1] = '0';
  formatted[2] = '.';
  const auto fractional_digits =
      static_cast<std::size_t>(exponent_marker - decimal - 1);
  if (fractional_digits != 8) {
    throw ValidationError("unable to format floating-point value as E16.9");
  }
  formatted[3] = *begin;
  std::copy_n(decimal + 1, 8, formatted + 4);
  if (exponent_magnitude <= 99) {
    formatted[12] = 'E';
    formatted[13] = exponent < 0 ? '-' : '+';
    formatted[14] = static_cast<char>('0' + exponent_magnitude / 10);
    formatted[15] = static_cast<char>('0' + exponent_magnitude % 10);
  } else {
    formatted[12] = exponent < 0 ? '-' : '+';
    formatted[13] = static_cast<char>('0' + exponent_magnitude / 100);
    formatted[14] = static_cast<char>('0' + exponent_magnitude / 10 % 10);
    formatted[15] = static_cast<char>('0' + exponent_magnitude % 10);
  }
  output.append(formatted, sizeof(formatted));
}

std::string format_e16_9(double value) {
  std::string result;
  result.reserve(16);
  append_e16_9(result, value);
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

  // Canonical EFIT arrays use five contiguous E16.9 values per line. Parsing
  // those records in place avoids one temporary string and one locale stream
  // per value. The strict shape check keeps unusual whitespace-separated or
  // producer-specific records on the compatibility path below.
  if (count != 0) {
    auto position = position_;
    result.resize(count);
    bool fixed_width = true;
    for (std::size_t index = 0; index < count && fixed_width; ++index) {
      if (position + 16 > input_.size()) {
        fixed_width = false;
        break;
      }
      const auto token = std::string_view(input_.data() + position, 16);
      if (!parse_fortran_real(token, result[index])) {
        fixed_width = false;
        break;
      }
      position += 16;
      const bool end_of_record = ((index + 1) % 5 == 0) || index + 1 == count;
      if (end_of_record) {
        if (position < input_.size() && input_[position] == '\r') {
          ++position;
        }
        if (position >= input_.size() || input_[position] != '\n') {
          fixed_width = false;
          break;
        }
        ++position;
      }
    }
    if (fixed_width) {
      position_ = position;
      return result;
    }
    result.clear();
  }

  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(next_real(field + "[" + std::to_string(index) + "]"));
  }
  return result;
}

void FortranRealWriter::value(double number) {
  append_e16_9(output_, number);
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
