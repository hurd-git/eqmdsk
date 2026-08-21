#include "eqmdsk/sfile.hpp"

#include <Eigen/Core>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <vector>

#include "detail/fortran.hpp"
#include "eqmdsk/error.hpp"

namespace eqmdsk {
namespace {

template <typename T>
const T& require(const FieldMap& fields, const char* name) {
  const auto& value = fields.at(name);
  if (!std::holds_alternative<T>(value)) {
    throw ValidationError(std::string(name) + " has type " +
                          field_type_name(value) +
                          ", expected a different type");
  }
  return std::get<T>(value);
}

bool line_space(char value) noexcept {
  return value == ' ' || value == '\t' || value == '\v' || value == '\f';
}

std::vector<std::string_view> split_tokens(std::string_view line) {
  std::vector<std::string_view> result;
  std::size_t position = 0;
  while (position < line.size()) {
    while (position < line.size() && line_space(line[position])) {
      ++position;
    }
    if (position == line.size()) {
      break;
    }
    const auto start = position;
    while (position < line.size() && !line_space(line[position])) {
      ++position;
    }
    result.push_back(line.substr(start, position - start));
  }
  return result;
}

struct ParsedToken {
  bool numeric = false;
  bool valid = false;
  double value = 0.0;
};

bool decimal_real_syntax(std::string_view token) noexcept {
  std::size_t position = 0;
  if (position < token.size() &&
      (token[position] == '+' || token[position] == '-')) {
    ++position;
  }
  const auto integer_start = position;
  while (position < token.size() && token[position] >= '0' &&
         token[position] <= '9') {
    ++position;
  }
  bool has_digit = position != integer_start;
  if (position < token.size() && token[position] == '.') {
    ++position;
    const auto fraction_start = position;
    while (position < token.size() && token[position] >= '0' &&
           token[position] <= '9') {
      ++position;
    }
    has_digit = has_digit || position != fraction_start;
  }
  if (!has_digit) {
    return false;
  }
  if (position < token.size() &&
      (token[position] == 'e' || token[position] == 'E' ||
       token[position] == 'd' || token[position] == 'D')) {
    ++position;
    if (position < token.size() &&
        (token[position] == '+' || token[position] == '-')) {
      ++position;
    }
    const auto exponent_start = position;
    while (position < token.size() && token[position] >= '0' &&
           token[position] <= '9') {
      ++position;
    }
    if (position == exponent_start) {
      return false;
    }
  }
  return position == token.size();
}

ParsedToken parse_token(std::string_view token) {
  ParsedToken result;
  result.numeric = decimal_real_syntax(token);
  result.valid = result.numeric &&
                 detail::parse_fortran_real(token, result.value);
  return result;
}

bool has_numeric_prefix(std::string_view token) noexcept {
  std::size_t position = 0;
  if (position < token.size() &&
      (token[position] == '+' || token[position] == '-')) {
    ++position;
  }
  if (position < token.size() && token[position] >= '0' &&
      token[position] <= '9') {
    return true;
  }
  return position + 1 < token.size() && token[position] == '.' &&
         token[position + 1] >= '0' && token[position + 1] <= '9';
}

enum class LineKind { Text, Data, MalformedData };

struct ParsedLine {
  LineKind kind = LineKind::Text;
  double values[4]{};
  std::string error;
};

ParsedLine parse_line(std::string_view line, bool data_started) {
  const auto tokens = split_tokens(line);
  if (tokens.empty()) {
    return {};
  }

  std::vector<ParsedToken> parsed;
  parsed.reserve(tokens.size());
  bool all_numeric = true;
  for (const auto token : tokens) {
    parsed.push_back(parse_token(token));
    all_numeric = all_numeric && parsed.back().numeric;
  }

  if (tokens.size() == 4 && all_numeric) {
    ParsedLine result;
    for (std::size_t index = 0; index < 4; ++index) {
      if (!parsed[index].valid) {
        result.kind = LineKind::MalformedData;
        result.error = "S-file data values must be finite and representable";
        return result;
      }
      result.values[index] = parsed[index].value;
    }
    result.kind = LineKind::Data;
    return result;
  }

  // A wholly numeric line is unambiguously a damaged data record. A mixed line
  // whose first token is itself a valid number is likewise treated as damaged.
  if (all_numeric) {
    ParsedLine result;
    result.kind = LineKind::MalformedData;
    result.error = "S-file data rows must contain exactly four values";
    return result;
  }
  if (parsed.front().numeric || has_numeric_prefix(tokens.front())) {
    if (data_started && !parsed.front().numeric) {
      return {};
    }
    ParsedLine result;
    result.kind = LineKind::MalformedData;
    result.error = "invalid floating-point value in S-file data row";
    return result;
  }
  return {};
}

DoubleVector to_vector(const std::vector<double>& values) {
  DoubleVector result(static_cast<Eigen::Index>(values.size()));
  for (std::size_t index = 0; index < values.size(); ++index) {
    result[static_cast<Eigen::Index>(index)] = values[index];
  }
  return result;
}

bool ends_in_line_break(const std::string& value) noexcept {
  return !value.empty() &&
         (value.back() == '\n' || value.back() == '\r');
}

void ensure_record_boundary(std::string& output) {
  if (!output.empty() && !ends_in_line_break(output)) {
    output.push_back('\n');
  }
}

}  // namespace

SFile::SFile(std::string filename) : FieldFile(std::move(filename)) {
  parse(detail::read_binary_file(filename_));
}

void SFile::parse(const std::string& bytes) {
  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> dx;
  std::vector<double> dy;
  std::vector<std::string> labels;

  std::size_t offset = 0;
  std::size_t line_number = 1;
  bool data_started = false;
  while (offset < bytes.size()) {
    const auto terminator = bytes.find_first_of("\r\n", offset);
    const auto content_end =
        terminator == std::string::npos ? bytes.size() : terminator;
    auto record_end = content_end;
    if (terminator != std::string::npos) {
      record_end = terminator + 1;
      if (bytes[terminator] == '\r' && record_end < bytes.size() &&
          bytes[record_end] == '\n') {
        ++record_end;
      }
    }
    const std::string_view content(bytes.data() + offset,
                                   content_end - offset);
    const auto parsed = parse_line(content, data_started);

    if (parsed.kind == LineKind::Data) {
      data_started = true;
      x.push_back(parsed.values[0]);
      y.push_back(parsed.values[1]);
      dx.push_back(parsed.values[2]);
      dy.push_back(parsed.values[3]);
    } else if (parsed.kind == LineKind::MalformedData) {
      throw ParseError(parsed.error, detail::path_for_diagnostic(filename_),
                       line_number, 1);
    } else if (!data_started && labels.size() < 3) {
      labels.emplace_back(content);
    } else {
      // Non-data records outside the leading labels are accepted for broad
      // compatibility but omitted by the canonical writer.
    }

    offset = record_end;
    ++line_number;
  }

  static constexpr const char* label_names[]{"XLABEL", "YLABEL", "TITLE"};
  for (std::size_t index = 0; index < labels.size(); ++index) {
    fields_.insert(label_names[index], std::move(labels[index]), true, index);
  }
  fields_.insert("X", to_vector(x), true, 3);
  fields_.insert("Y", to_vector(y), true, 4);
  fields_.insert("DX", to_vector(dx), true, 5);
  fields_.insert("DY", to_vector(dy), true, 6);
}

void SFile::validate_for_write() const {
  const auto& x = require<DoubleVector>(fields_, "X");
  const auto& y = require<DoubleVector>(fields_, "Y");
  const auto& dx = require<DoubleVector>(fields_, "DX");
  const auto& dy = require<DoubleVector>(fields_, "DY");
  if (y.size() != x.size() || dx.size() != x.size() ||
      dy.size() != x.size()) {
    throw ValidationError("X, Y, DX, and DY must have equal lengths");
  }

  for (const auto* name : {"X", "Y", "DX", "DY"}) {
    const auto& values = require<DoubleVector>(fields_, name);
    for (Eigen::Index index = 0; index < values.size(); ++index) {
      if (!std::isfinite(values[index])) {
        throw ValidationError(std::string(name) +
                              " must contain only finite values");
      }
    }
  }

  const bool has_xlabel = fields_.contains("XLABEL");
  const bool has_ylabel = fields_.contains("YLABEL");
  const bool has_title = fields_.contains("TITLE");
  if ((has_ylabel && !has_xlabel) ||
      (has_title && (!has_xlabel || !has_ylabel))) {
    throw ValidationError(
        "S-file title fields must be contiguous from XLABEL to TITLE");
  }
  for (const auto* name : {"XLABEL", "YLABEL", "TITLE"}) {
    if (!fields_.contains(name)) {
      continue;
    }
    const auto& value = require<std::string>(fields_, name);
    if (value.find_first_of("\r\n") != std::string::npos) {
      throw ValidationError(std::string(name) +
                            " must contain exactly one text line");
    }
  }
}

void SFile::write(const std::string& path) const {
  validate_for_write();
  const auto& x = require<DoubleVector>(fields_, "X");
  const auto& y = require<DoubleVector>(fields_, "Y");
  const auto& dx = require<DoubleVector>(fields_, "DX");
  const auto& dy = require<DoubleVector>(fields_, "DY");

  std::string output;
  for (const auto* name : {"XLABEL", "YLABEL", "TITLE"}) {
    if (fields_.contains(name)) {
      output += require<std::string>(fields_, name);
      output.push_back('\n');
    }
  }

  std::ostringstream row;
  row.imbue(std::locale::classic());
  row << std::setprecision(std::numeric_limits<double>::max_digits10);
  const auto count = static_cast<std::size_t>(x.size());
  for (std::size_t data_index = 0; data_index < count; ++data_index) {
    ensure_record_boundary(output);
    row.str({});
    row.clear();
    row << x[static_cast<Eigen::Index>(data_index)] << ' '
        << y[static_cast<Eigen::Index>(data_index)] << ' '
        << dx[static_cast<Eigen::Index>(data_index)] << ' '
        << dy[static_cast<Eigen::Index>(data_index)] << '\n';
    output += row.str();
  }
  detail::write_binary_file(path, output);
}

}  // namespace eqmdsk
