#include "eqmdsk/kfile.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "detail/fortran.hpp"
#include "eqmdsk/error.hpp"

namespace eqmdsk {
namespace {

bool ascii_alpha(char value) noexcept {
  return (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z');
}

bool ascii_digit(char value) noexcept {
  return value >= '0' && value <= '9';
}

bool name_start(char value) noexcept {
  return ascii_alpha(value) || value == '_';
}

bool name_character(char value) noexcept {
  return name_start(value) || ascii_digit(value) || value == '%';
}

char ascii_upper_char(char value) noexcept {
  if (value >= 'a' && value <= 'z') {
    return static_cast<char>(value - 'a' + 'A');
  }
  return value;
}

std::string ascii_upper(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 ascii_upper_char);
  return result;
}

bool ascii_iequal(std::string_view left, std::string_view right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (ascii_upper_char(left[index]) != ascii_upper_char(right[index])) {
      return false;
    }
  }
  return true;
}

bool is_space(char value) noexcept {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

bool line_comment_start(std::string_view text, std::size_t position) noexcept {
  if (text[position] == '!' || text[position] == ';') {
    return true;
  }
  if (text[position] != '#') {
    return false;
  }
  auto probe = position;
  while (probe > 0 && text[probe - 1] != '\n' && text[probe - 1] != '\r') {
    --probe;
  }
  while (probe < position &&
         (text[probe] == ' ' || text[probe] == '\t')) {
    ++probe;
  }
  return probe == position;
}

std::size_t skip_quoted(std::string_view text, std::size_t position) noexcept {
  const char quote = text[position++];
  while (position < text.size()) {
    if (text[position] != quote) {
      ++position;
      continue;
    }
    if (position + 1 < text.size() && text[position + 1] == quote) {
      position += 2;
      continue;
    }
    return position + 1;
  }
  return text.size();
}

std::pair<std::size_t, std::size_t> line_and_column(
    std::string_view text, std::size_t position) noexcept {
  std::size_t line = 1;
  std::size_t column = 1;
  const auto end = std::min(position, text.size());
  for (std::size_t index = 0; index < end; ++index) {
    if (text[index] == '\n') {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  }
  return {line, column};
}

[[noreturn]] void parse_failure(const std::string& message,
                                const std::filesystem::path& filename,
                                std::string_view text,
                                std::size_t position) {
  const auto location = line_and_column(text, position);
  throw ParseError(message, detail::path_for_diagnostic(filename),
                   location.first, location.second);
}

struct GroupStart {
  std::size_t begin = 0;
  std::size_t body_begin = 0;
  char opener = '&';
  std::string original_name;
};

std::optional<GroupStart> find_group_start(std::string_view text,
                                           std::size_t position) {
  while (position < text.size()) {
    if (line_comment_start(text, position)) {
      const auto newline = text.find('\n', position + 1);
      position = newline == std::string_view::npos ? text.size() : newline + 1;
      continue;
    }
    if (text[position] == '\'' || text[position] == '"') {
      position = skip_quoted(text, position);
      continue;
    }
    if (text[position] != '&' && text[position] != '$') {
      ++position;
      continue;
    }
    if (position != 0 && !is_space(text[position - 1]) &&
        text[position - 1] != '/') {
      ++position;
      continue;
    }
    auto name_begin = position + 1;
    while (name_begin < text.size() &&
           (text[name_begin] == ' ' || text[name_begin] == '\t')) {
      ++name_begin;
    }
    if (name_begin >= text.size() || !name_start(text[name_begin])) {
      ++position;
      continue;
    }
    auto name_end = name_begin + 1;
    while (name_end < text.size() && name_character(text[name_end])) {
      ++name_end;
    }
    const auto name = std::string(text.substr(name_begin, name_end - name_begin));
    if (ascii_iequal(name, "END")) {
      position = name_end;
      continue;
    }
    return GroupStart{position, name_end, text[position], name};
  }
  return std::nullopt;
}

struct GroupEnd {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string spelling;
};

std::optional<GroupEnd> find_group_end(std::string_view text,
                                       std::size_t position) {
  std::size_t parenthesis_depth = 0;
  while (position < text.size()) {
    if (line_comment_start(text, position)) {
      const auto newline = text.find('\n', position + 1);
      position = newline == std::string_view::npos ? text.size() : newline + 1;
      continue;
    }
    if (text[position] == '\'' || text[position] == '"') {
      position = skip_quoted(text, position);
      continue;
    }
    if (text[position] == '(') {
      ++parenthesis_depth;
      ++position;
      continue;
    }
    if (text[position] == ')' && parenthesis_depth != 0) {
      --parenthesis_depth;
      ++position;
      continue;
    }
    if (parenthesis_depth == 0 && text[position] == '/') {
      return GroupEnd{position, position + 1, "/"};
    }
    if (parenthesis_depth == 0 &&
        (text[position] == '&' || text[position] == '$')) {
      auto probe = position + 1;
      while (probe < text.size() &&
             (text[probe] == ' ' || text[probe] == '\t')) {
        ++probe;
      }
      const auto word_begin = probe;
      while (probe < text.size() && ascii_alpha(text[probe])) {
        ++probe;
      }
      if (ascii_iequal(text.substr(word_begin, probe - word_begin), "END") &&
          (probe == text.size() || !name_character(text[probe]))) {
        return GroupEnd{position, probe,
                        std::string(text.substr(position, probe - position))};
      }
    }
    ++position;
  }
  return std::nullopt;
}

struct AssignmentStart {
  std::size_t begin = 0;
  std::size_t equals = 0;
  std::string original_name;
  std::string designator;
  std::string subscript;
};

std::vector<AssignmentStart> find_assignments(std::string_view body) {
  std::vector<AssignmentStart> result;
  std::size_t position = 0;
  while (position < body.size()) {
    if (line_comment_start(body, position)) {
      const auto newline = body.find('\n', position + 1);
      position = newline == std::string_view::npos ? body.size() : newline + 1;
      continue;
    }
    if (body[position] == '\'' || body[position] == '"') {
      position = skip_quoted(body, position);
      continue;
    }
    if (!name_start(body[position]) ||
        (position != 0 && !is_space(body[position - 1]) &&
         body[position - 1] != ',')) {
      ++position;
      continue;
    }

    const auto name_begin = position;
    auto name_end = position + 1;
    while (name_end < body.size() && name_character(body[name_end])) {
      ++name_end;
    }
    auto probe = name_end;
    while (probe < body.size() &&
           (body[probe] == ' ' || body[probe] == '\t')) {
      ++probe;
    }

    std::string subscript;
    if (probe < body.size() && body[probe] == '(') {
      const auto opening = probe++;
      std::size_t depth = 1;
      while (probe < body.size() && depth != 0) {
        if (body[probe] == '\'' || body[probe] == '"') {
          probe = skip_quoted(body, probe);
        } else if (body[probe] == '(') {
          ++depth;
          ++probe;
        } else if (body[probe] == ')') {
          --depth;
          ++probe;
        } else {
          ++probe;
        }
      }
      if (depth != 0) {
        position = name_end;
        continue;
      }
      subscript = detail::trim_copy(
          body.substr(opening + 1, probe - opening - 2));
      while (probe < body.size() &&
             (body[probe] == ' ' || body[probe] == '\t')) {
        ++probe;
      }
    }
    if (probe >= body.size() || body[probe] != '=' ||
        (probe + 1 < body.size() && body[probe + 1] == '=')) {
      position = name_end;
      continue;
    }

    const auto designator =
        detail::trim_copy(body.substr(name_begin, probe - name_begin));
    result.push_back(AssignmentStart{
        name_begin, probe,
        std::string(body.substr(name_begin, name_end - name_begin)),
        designator, subscript});
    position = probe + 1;
  }
  return result;
}

bool parse_integer_token(std::string_view token, std::int64_t& value) {
  const auto text = detail::trim_copy(token);
  if (text.empty()) {
    return false;
  }
  std::size_t position = 0;
  if (text[position] == '+' || text[position] == '-') {
    ++position;
  }
  const auto digit_begin = position;
  while (position < text.size() && ascii_digit(text[position])) {
    ++position;
  }
  if (position == digit_begin || position != text.size()) {
    return false;
  }
  try {
    std::size_t consumed = 0;
    value = std::stoll(text, &consumed, 10);
    return consumed == text.size();
  } catch (...) {
    return false;
  }
}

bool parse_real_token(std::string_view token, double& value) {
  const auto text = detail::trim_copy(token);
  if (text.empty()) {
    return false;
  }
  bool real_marker = false;
  for (const char character : text) {
    if (character == 'D' || character == 'd') {
      real_marker = true;
    } else if (character == '.' || character == 'E' || character == 'e') {
      real_marker = true;
    }
  }
  if (!real_marker) {
    return false;
  }
  return detail::parse_fortran_real(text, value);
}

bool parse_any_real(std::string_view token, double& value) {
  std::int64_t integer = 0;
  if (parse_integer_token(token, integer)) {
    value = static_cast<double>(integer);
    return true;
  }
  return parse_real_token(token, value);
}

std::optional<std::size_t> matching_parenthesis(std::string_view text,
                                                std::size_t position) {
  if (position >= text.size() || text[position] != '(') {
    return std::nullopt;
  }
  std::size_t depth = 1;
  ++position;
  while (position < text.size()) {
    if (text[position] == '\'' || text[position] == '"') {
      position = skip_quoted(text, position);
    } else if (text[position] == '(') {
      ++depth;
      ++position;
    } else if (text[position] == ')') {
      --depth;
      if (depth == 0) {
        return position + 1;
      }
      ++position;
    } else {
      ++position;
    }
  }
  return std::nullopt;
}

struct ParsedToken {
  NamelistValueKind kind = NamelistValueKind::raw;
  NamelistValue::Storage storage = std::string{};
  std::size_t repeat = 1;
  std::string original_text;
  std::size_t end = 0;
};

ParsedToken parse_scalar(std::string_view text, std::size_t begin,
                         std::size_t repeat) {
  ParsedToken result;
  result.repeat = repeat;
  const auto scalar_begin = begin;

  if (text[begin] == '\'' || text[begin] == '"') {
    const char quote = text[begin++];
    std::string value;
    bool terminated = false;
    while (begin < text.size()) {
      if (text[begin] != quote) {
        value.push_back(text[begin++]);
      } else if (begin + 1 < text.size() && text[begin + 1] == quote) {
        value.push_back(quote);
        begin += 2;
      } else {
        ++begin;
        terminated = true;
        break;
      }
    }
    if (terminated) {
      result.kind = NamelistValueKind::string;
      result.storage = std::move(value);
    } else {
      result.kind = NamelistValueKind::raw;
      result.storage = std::string(text.substr(scalar_begin, begin - scalar_begin));
    }
    result.end = begin;
    result.original_text =
        std::string(text.substr(scalar_begin, begin - scalar_begin));
    return result;
  }

  if (text[begin] == '(') {
    const auto end = matching_parenthesis(text, begin).value_or(text.size());
    const auto inside = text.substr(begin + 1, end - begin >= 2
                                                   ? end - begin - 2
                                                   : 0);
    std::size_t comma = std::string_view::npos;
    std::size_t depth = 0;
    for (std::size_t index = 0; index < inside.size(); ++index) {
      if (inside[index] == '\'' || inside[index] == '"') {
        index = skip_quoted(inside, index) - 1;
      } else if (inside[index] == '(') {
        ++depth;
      } else if (inside[index] == ')' && depth != 0) {
        --depth;
      } else if (inside[index] == ',' && depth == 0) {
        if (comma != std::string_view::npos) {
          comma = std::string_view::npos;
          break;
        }
        comma = index;
      }
    }
    double real = 0.0;
    double imaginary = 0.0;
    if (comma != std::string_view::npos &&
        parse_any_real(inside.substr(0, comma), real) &&
        parse_any_real(inside.substr(comma + 1), imaginary)) {
      result.kind = NamelistValueKind::complex;
      result.storage = std::complex<double>(real, imaginary);
    } else {
      result.kind = NamelistValueKind::raw;
      result.storage = std::string(text.substr(begin, end - begin));
    }
    result.end = end;
    result.original_text = std::string(text.substr(begin, end - begin));
    return result;
  }

  auto end = begin;
  while (end < text.size() && !is_space(text[end]) && text[end] != ',' &&
         !line_comment_start(text, end)) {
    ++end;
  }
  const auto token = text.substr(begin, end - begin);
  const auto upper = ascii_upper(token);
  std::int64_t integer = 0;
  double real = 0.0;
  if (upper == ".TRUE." || upper == ".T." || upper == "TRUE" ||
      upper == "T") {
    result.kind = NamelistValueKind::logical;
    result.storage = true;
  } else if (upper == ".FALSE." || upper == ".F." || upper == "FALSE" ||
             upper == "F") {
    result.kind = NamelistValueKind::logical;
    result.storage = false;
  } else if (parse_integer_token(token, integer)) {
    result.kind = NamelistValueKind::integer;
    result.storage = integer;
  } else if (parse_real_token(token, real)) {
    result.kind = NamelistValueKind::real;
    result.storage = real;
  } else {
    result.kind = NamelistValueKind::raw;
    result.storage = std::string(token);
  }
  result.end = end;
  result.original_text = std::string(token);
  return result;
}

struct ParsedValues {
  std::vector<ParsedToken> values;
  std::size_t last_end = 0;
};

ParsedValues parse_values(std::string_view text) {
  ParsedValues result;
  std::size_t position = 0;
  bool expecting_value_after_comma = true;
  bool have_any_value = false;
  while (position < text.size()) {
    if (is_space(text[position])) {
      ++position;
      continue;
    }
    if (line_comment_start(text, position)) {
      const auto newline = text.find('\n', position + 1);
      position = newline == std::string_view::npos ? text.size() : newline + 1;
      continue;
    }
    // Free-form continuation ampersands are syntax, not values.
    if (text[position] == '&') {
      auto probe = position + 1;
      while (probe < text.size() &&
             (text[probe] == ' ' || text[probe] == '\t' ||
              text[probe] == '\r')) {
        ++probe;
      }
      if (probe == text.size() || text[probe] == '\n' ||
          line_comment_start(text, probe)) {
        position = probe;
        continue;
      }
    }

    if (text[position] == ',') {
      if (expecting_value_after_comma) {
        ParsedToken null_value;
        null_value.kind = NamelistValueKind::null;
        null_value.storage = std::string{};
        null_value.original_text.clear();
        null_value.end = position;
        result.values.push_back(std::move(null_value));
        have_any_value = true;
      }
      expecting_value_after_comma = true;
      result.last_end = position + 1;
      ++position;
      continue;
    }

    const auto value_begin = position;
    std::size_t repeat = 1;
    bool had_repeat = false;
    if (ascii_digit(text[position])) {
      auto probe = position;
      while (probe < text.size() && ascii_digit(text[probe])) {
        ++probe;
      }
      if (probe < text.size() && text[probe] == '*') {
        try {
          const auto parsed = std::stoull(
              std::string(text.substr(position, probe - position)));
          if (parsed <= std::numeric_limits<std::size_t>::max()) {
            repeat = static_cast<std::size_t>(parsed);
            position = probe + 1;
            had_repeat = true;
          }
        } catch (...) {
          // Preserve an overflowing repetition as an opaque token below.
        }
      }
    }
    if (had_repeat &&
        (position >= text.size() || is_space(text[position]) ||
         text[position] == ',' || line_comment_start(text, position))) {
      ParsedToken null_value;
      null_value.kind = NamelistValueKind::null;
      null_value.storage = std::string{};
      null_value.repeat = repeat;
      null_value.original_text =
          std::string(text.substr(value_begin, position - value_begin));
      null_value.end = position;
      result.values.push_back(std::move(null_value));
      result.last_end = position;
      have_any_value = true;
      expecting_value_after_comma = false;
      continue;
    }

    auto parsed = parse_scalar(text, position, repeat);
    parsed.original_text =
        std::string(text.substr(value_begin, parsed.end - value_begin));
    result.last_end = parsed.end;
    result.values.push_back(std::move(parsed));
    position = result.last_end;
    have_any_value = true;
    expecting_value_after_comma = false;
  }
  static_cast<void>(have_any_value);
  return result;
}

std::string comments_before(std::string_view text, std::size_t end) {
  std::string result;
  std::size_t position = 0;
  end = std::min(end, text.size());
  while (position < end) {
    if (text[position] == '\'' || text[position] == '"') {
      position = std::min(skip_quoted(text, position), end);
      continue;
    }
    if (line_comment_start(text, position)) {
      auto comment_end = text.find('\n', position + 1);
      if (comment_end == std::string_view::npos || comment_end >= end) {
        comment_end = end;
      } else {
        ++comment_end;
      }
      result.append(text.substr(position, comment_end - position));
      if (!result.empty() && result.back() != '\n') {
        result.push_back('\n');
      }
      position = comment_end;
      continue;
    }
    ++position;
  }
  return result;
}

NamelistValue make_value(const ParsedToken& parsed) {
  NamelistValue value = [&]() {
    switch (parsed.kind) {
      case NamelistValueKind::null:
        return NamelistValue::null(parsed.repeat);
      case NamelistValueKind::integer:
        return NamelistValue::integer(std::get<std::int64_t>(parsed.storage),
                                      parsed.repeat);
      case NamelistValueKind::real:
        return NamelistValue::real(std::get<double>(parsed.storage),
                                   parsed.repeat);
      case NamelistValueKind::logical:
        return NamelistValue::logical(std::get<bool>(parsed.storage),
                                      parsed.repeat);
      case NamelistValueKind::string:
        return NamelistValue::string(std::get<std::string>(parsed.storage),
                                     parsed.repeat);
      case NamelistValueKind::complex:
        return NamelistValue::complex(
            std::get<std::complex<double>>(parsed.storage), parsed.repeat);
      case NamelistValueKind::raw:
        return NamelistValue::raw(std::get<std::string>(parsed.storage),
                                  parsed.repeat);
    }
    return NamelistValue::raw({}, parsed.repeat);
  }();
  // This function deliberately cannot alter original_text_; the caller is a
  // KFile member (and therefore a friend) and fills it after construction.
  return value;
}

bool same_double(double left, double right) noexcept {
  return left == right || (std::isnan(left) && std::isnan(right));
}

bool field_values_equal(const FieldValue& left, const FieldValue& right) {
  if (left.index() != right.index()) {
    return false;
  }
  return std::visit(
      [](const auto& lhs, const auto& rhs) {
        using Left = std::decay_t<decltype(lhs)>;
        using Right = std::decay_t<decltype(rhs)>;
        if constexpr (!std::is_same_v<Left, Right>) {
          return false;
        } else if constexpr (std::is_same_v<Left, double>) {
          return same_double(lhs, rhs);
        } else if constexpr (std::is_same_v<Left, DoubleVector> ||
                             std::is_same_v<Left, IntVector> ||
                             std::is_same_v<Left, DoubleMatrix>) {
          if (lhs.rows() != rhs.rows() || lhs.cols() != rhs.cols()) {
            return false;
          }
          for (Eigen::Index index = 0; index < lhs.size(); ++index) {
            if constexpr (std::is_same_v<typename Left::Scalar, double>) {
              if (!same_double(lhs.data()[index], rhs.data()[index])) {
                return false;
              }
            } else if (lhs.data()[index] != rhs.data()[index]) {
              return false;
            }
          }
          return true;
        } else {
          return lhs == rhs;
        }
      },
      left, right);
}

bool assign_preserving_array_storage(FieldValue& target,
                                     const FieldValue& source) {
  if (target.index() != source.index()) {
    return false;
  }
  return std::visit(
      [](auto& destination, const auto& incoming) {
        using Destination = std::decay_t<decltype(destination)>;
        using Incoming = std::decay_t<decltype(incoming)>;
        if constexpr (!std::is_same_v<Destination, Incoming>) {
          return false;
        } else if constexpr (std::is_same_v<Destination, DoubleVector> ||
                             std::is_same_v<Destination, IntVector> ||
                             std::is_same_v<Destination, DoubleMatrix>) {
          if (destination.rows() != incoming.rows() ||
              destination.cols() != incoming.cols()) {
            return false;
          }
          std::copy_n(incoming.data(), incoming.size(), destination.data());
          return true;
        } else {
          destination = incoming;
          return true;
        }
      },
      target, source);
}

constexpr std::size_t kMaximumExpandedValues = 10000000;
constexpr std::size_t kMaximumExpandedStringBytes = 64 * 1024 * 1024;

std::size_t field_element_count(const FieldValue& field) {
  return std::visit(
      [](const auto& value) -> std::size_t {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, IntVector> ||
                      std::is_same_v<T, DoubleVector> ||
                      std::is_same_v<T, DoubleMatrix>) {
          return static_cast<std::size_t>(value.size());
        } else if constexpr (std::is_same_v<T, StringVector>) {
          return value.size();
        } else {
          return 1;
        }
      },
      field);
}

std::size_t field_string_storage_bytes(const FieldValue& field) noexcept {
  return std::visit(
      [](const auto& value) -> std::size_t {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::string>) {
          if (value.size() > std::numeric_limits<std::size_t>::max() -
                                 sizeof(std::string)) {
            return std::numeric_limits<std::size_t>::max();
          }
          return sizeof(std::string) + value.size();
        } else if constexpr (std::is_same_v<T, StringVector>) {
          std::size_t total = 0;
          for (const auto& item : value) {
            const auto remaining =
                std::numeric_limits<std::size_t>::max() - total;
            if (item.size() > remaining ||
                sizeof(std::string) > remaining - item.size()) {
              return std::numeric_limits<std::size_t>::max();
            }
            total += sizeof(std::string) + item.size();
          }
          return total;
        } else {
          return 0;
        }
      },
      field);
}

std::optional<FieldValue> field_from_entry(
    const NamelistEntry& entry,
    std::size_t maximum_expanded_values = kMaximumExpandedValues,
    std::size_t maximum_string_bytes = kMaximumExpandedStringBytes) {
  if (!entry.subscript().empty() || entry.values().empty()) {
    return std::nullopt;
  }
  std::size_t count = 0;
  for (const auto& value : entry.values()) {
    if (value.repeat() > maximum_expanded_values - count) {
      return std::nullopt;
    }
    count += value.repeat();
  }
  if (count == 0) {
    return std::nullopt;
  }

  bool all_integer = true;
  bool all_numeric = true;
  bool all_string = true;
  bool all_logical = true;
  for (const auto& value : entry.values()) {
    all_integer = all_integer && value.kind() == NamelistValueKind::integer;
    all_numeric = all_numeric &&
                  (value.kind() == NamelistValueKind::integer ||
                   value.kind() == NamelistValueKind::real);
    all_string = all_string && value.kind() == NamelistValueKind::string;
    all_logical = all_logical && value.kind() == NamelistValueKind::logical;
  }

  if (all_string) {
    std::size_t projected_bytes = 0;
    for (const auto& value : entry.values()) {
      const auto& item = value.as_string();
      if (item.size() > std::numeric_limits<std::size_t>::max() -
                            sizeof(std::string)) {
        return std::nullopt;
      }
      const auto bytes_per_item = sizeof(std::string) + item.size();
      if (value.repeat() != 0 &&
          bytes_per_item >
              (maximum_string_bytes - projected_bytes) / value.repeat()) {
        return std::nullopt;
      }
      projected_bytes += bytes_per_item * value.repeat();
    }
  }

  if (count == 1 && entry.values().size() == 1 &&
      entry.values().front().repeat() == 1) {
    const auto& value = entry.values().front();
    switch (value.kind()) {
      case NamelistValueKind::null:
        return std::nullopt;
      case NamelistValueKind::integer:
        return FieldValue(value.as_integer());
      case NamelistValueKind::real:
        return FieldValue(value.as_real());
      case NamelistValueKind::logical:
        return FieldValue(value.as_logical());
      case NamelistValueKind::string:
        return FieldValue(value.as_string());
      default:
        return std::nullopt;
    }
  }
  if (all_integer) {
    IntVector result(static_cast<Eigen::Index>(count));
    Eigen::Index output = 0;
    for (const auto& value : entry.values()) {
      for (std::size_t repeat = 0; repeat < value.repeat(); ++repeat) {
        result[output++] = value.as_integer();
      }
    }
    return FieldValue(std::move(result));
  }
  if (all_numeric) {
    DoubleVector result(static_cast<Eigen::Index>(count));
    Eigen::Index output = 0;
    for (const auto& value : entry.values()) {
      const double number = value.kind() == NamelistValueKind::integer
                                ? static_cast<double>(value.as_integer())
                                : value.as_real();
      for (std::size_t repeat = 0; repeat < value.repeat(); ++repeat) {
        result[output++] = number;
      }
    }
    return FieldValue(std::move(result));
  }
  if (all_string) {
    StringVector result;
    result.reserve(count);
    for (const auto& value : entry.values()) {
      for (std::size_t repeat = 0; repeat < value.repeat(); ++repeat) {
        result.push_back(value.as_string());
      }
    }
    return FieldValue(std::move(result));
  }
  // FieldValue intentionally has no logical-vector or complex alternative;
  // the ordered namelist model remains authoritative for those entries.
  static_cast<void>(all_logical);
  return std::nullopt;
}

std::string format_real(double value) {
  if (!std::isfinite(value)) {
    throw ValidationError("namelist floating-point values must be finite");
  }
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::uppercase << std::scientific
         << std::setprecision(std::numeric_limits<double>::max_digits10 - 1)
         << value;
  return output.str();
}

std::string quote_string(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('\'');
  for (const char character : value) {
    result.push_back(character);
    if (character == '\'') {
      result.push_back('\'');
    }
  }
  result.push_back('\'');
  return result;
}

std::string serialize_value(const NamelistValue& value) {
  if (value.kind() == NamelistValueKind::null) {
    // An empty final token is indistinguishable from an ordinary trailing
    // separator.  The explicit repetition spelling is unambiguous even for a
    // single null value.
    return std::to_string(value.repeat()) + "*";
  }
  std::string result;
  if (value.repeat() != 1) {
    result += std::to_string(value.repeat());
    result.push_back('*');
  }
  switch (value.kind()) {
    case NamelistValueKind::null:
      break;  // Handled above to retain a trailing/single null value.
    case NamelistValueKind::integer:
      result += std::to_string(value.as_integer());
      break;
    case NamelistValueKind::real:
      result += format_real(value.as_real());
      break;
    case NamelistValueKind::logical:
      result += value.as_logical() ? ".TRUE." : ".FALSE.";
      break;
    case NamelistValueKind::string:
      result += quote_string(value.as_string());
      break;
    case NamelistValueKind::complex: {
      const auto number = value.as_complex();
      result.push_back('(');
      result += format_real(number.real());
      result += ", ";
      result += format_real(number.imag());
      result.push_back(')');
      break;
    }
    case NamelistValueKind::raw:
      result += value.as_raw();
      break;
  }
  return result;
}

std::vector<NamelistValue> values_from_field(const FieldValue& field) {
  return std::visit(
      [](const auto& value) -> std::vector<NamelistValue> {
        using T = std::decay_t<decltype(value)>;
        std::vector<NamelistValue> result;
        if constexpr (std::is_same_v<T, bool>) {
          result.push_back(NamelistValue::logical(value));
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          result.push_back(NamelistValue::integer(value));
        } else if constexpr (std::is_same_v<T, double>) {
          result.push_back(NamelistValue::real(value));
        } else if constexpr (std::is_same_v<T, std::string>) {
          result.push_back(NamelistValue::string(value));
        } else if constexpr (std::is_same_v<T, IntVector>) {
          result.reserve(static_cast<std::size_t>(value.size()));
          for (Eigen::Index index = 0; index < value.size(); ++index) {
            result.push_back(NamelistValue::integer(value[index]));
          }
        } else if constexpr (std::is_same_v<T, DoubleVector>) {
          result.reserve(static_cast<std::size_t>(value.size()));
          for (Eigen::Index index = 0; index < value.size(); ++index) {
            result.push_back(NamelistValue::real(value[index]));
          }
        } else if constexpr (std::is_same_v<T, StringVector>) {
          result.reserve(value.size());
          for (const auto& item : value) {
            result.push_back(NamelistValue::string(item));
          }
        } else {
          throw ValidationError(
              "two-dimensional fields cannot be written as namelist values");
        }
        return result;
      },
      field);
}

}  // namespace

NamelistValue::NamelistValue(NamelistValueKind kind, Storage storage,
                             std::size_t repeat, std::string original_text)
    : kind_(kind),
      storage_(std::move(storage)),
      repeat_(repeat),
      original_text_(std::move(original_text)) {}

NamelistValue NamelistValue::null(std::size_t repeat) {
  return NamelistValue(NamelistValueKind::null, std::string{}, repeat);
}

NamelistValue NamelistValue::integer(std::int64_t value,
                                     std::size_t repeat) {
  return NamelistValue(NamelistValueKind::integer, value, repeat);
}

NamelistValue NamelistValue::real(double value, std::size_t repeat) {
  return NamelistValue(NamelistValueKind::real, value, repeat);
}

NamelistValue NamelistValue::logical(bool value, std::size_t repeat) {
  return NamelistValue(NamelistValueKind::logical, value, repeat);
}

NamelistValue NamelistValue::string(std::string value, std::size_t repeat) {
  return NamelistValue(NamelistValueKind::string, std::move(value), repeat);
}

NamelistValue NamelistValue::complex(std::complex<double> value,
                                     std::size_t repeat) {
  return NamelistValue(NamelistValueKind::complex, value, repeat);
}

NamelistValue NamelistValue::raw(std::string value, std::size_t repeat) {
  return NamelistValue(NamelistValueKind::raw, std::move(value), repeat);
}

std::int64_t NamelistValue::as_integer() const {
  if (kind_ != NamelistValueKind::integer) {
    throw FieldError("namelist value is not an integer");
  }
  return std::get<std::int64_t>(storage_);
}

double NamelistValue::as_real() const {
  if (kind_ != NamelistValueKind::real) {
    throw FieldError("namelist value is not a real number");
  }
  return std::get<double>(storage_);
}

bool NamelistValue::as_logical() const {
  if (kind_ != NamelistValueKind::logical) {
    throw FieldError("namelist value is not logical");
  }
  return std::get<bool>(storage_);
}

const std::string& NamelistValue::as_string() const {
  if (kind_ != NamelistValueKind::string) {
    throw FieldError("namelist value is not a string");
  }
  return std::get<std::string>(storage_);
}

const std::complex<double>& NamelistValue::as_complex() const {
  if (kind_ != NamelistValueKind::complex) {
    throw FieldError("namelist value is not complex");
  }
  return std::get<std::complex<double>>(storage_);
}

const std::string& NamelistValue::as_raw() const {
  if (kind_ != NamelistValueKind::raw) {
    throw FieldError("namelist value is not opaque text");
  }
  return std::get<std::string>(storage_);
}

bool NamelistValue::operator==(const NamelistValue& other) const noexcept {
  if (kind_ != other.kind_ || repeat_ != other.repeat_ ||
      storage_.index() != other.storage_.index()) {
    return false;
  }
  switch (kind_) {
    case NamelistValueKind::real:
      return same_double(std::get<double>(storage_),
                         std::get<double>(other.storage_));
    case NamelistValueKind::complex: {
      const auto left = std::get<std::complex<double>>(storage_);
      const auto right = std::get<std::complex<double>>(other.storage_);
      return same_double(left.real(), right.real()) &&
             same_double(left.imag(), right.imag());
    }
    default:
      return storage_ == other.storage_;
  }
}

bool NamelistEntry::modified() const noexcept {
  return explicitly_modified_ || values_ != original_values_;
}

std::size_t NamelistSection::count(const std::string& name) const {
  const auto normalized = ascii_upper(name);
  return static_cast<std::size_t>(std::count_if(
      entries_.begin(), entries_.end(), [&](const NamelistEntry& item) {
        return item.name() == normalized;
      }));
}

const NamelistEntry& NamelistSection::entry(const std::string& name,
                                            std::size_t occurrence) const {
  const auto normalized = ascii_upper(name);
  for (const auto& item : entries_) {
    if (item.name() == normalized) {
      if (occurrence == 0) {
        return item;
      }
      --occurrence;
    }
  }
  throw FieldError("unknown namelist entry: " + normalized);
}

KFile::KFile(const std::filesystem::path& filename) : EFITFile(filename) {
  parse(detail::read_binary_file(filename));
}

void KFile::parse(const std::string& bytes) {
  sections_.clear();
  outside_text_.clear();
  raw_sections_.clear();
  fields_.clear();
  field_targets_.clear();

  std::size_t cursor = 0;
  while (const auto start = find_group_start(bytes, cursor)) {
    const auto ending = find_group_end(bytes, start->body_begin);
    if (!ending) {
      parse_failure("unterminated namelist section " + start->original_name,
                    filename_, bytes, start->begin);
    }

    outside_text_.push_back(bytes.substr(cursor, start->begin - cursor));
    if (!outside_text_.back().empty()) {
      raw_sections_.push_back(RawSection{
          "outside_" + std::to_string(outside_text_.size() - 1),
          outside_text_.back(), cursor, false});
    }

    NamelistSection section;
    section.name_ = ascii_upper(start->original_name);
    section.original_name_ = start->original_name;
    section.opener_ = start->opener;
    section.terminator_ = ending->spelling;
    section.source_order_ = sections_.size();
    section.source_offset_ = start->begin;
    section.raw_text_ =
        bytes.substr(start->begin, ending->end - start->begin);

    const auto body = std::string_view(bytes).substr(
        start->body_begin, ending->begin - start->body_begin);
    const auto assignments = find_assignments(body);
    section.entries_.reserve(assignments.size());
    for (std::size_t index = 0; index < assignments.size(); ++index) {
      const auto& assignment = assignments[index];
      const auto body_end = index + 1 < assignments.size()
                                ? assignments[index + 1].begin
                                : body.size();
      const auto value_begin = assignment.equals + 1;
      const auto value_text = body.substr(value_begin, body_end - value_begin);
      const auto parsed = parse_values(value_text);

      NamelistEntry entry;
      entry.name_ = ascii_upper(assignment.original_name);
      entry.original_name_ = assignment.original_name;
      entry.designator_ = assignment.designator;
      entry.subscript_ = assignment.subscript;
      entry.source_order_ = index;
      entry.source_offset_ = start->body_begin + assignment.begin;
      entry.relative_begin_ = entry.source_offset_ - start->begin;
      entry.relative_end_ = start->body_begin + body_end - start->begin;
      entry.raw_text_ = std::string(body.substr(
          assignment.begin, body_end - assignment.begin));
      entry.parsed_ = !parsed.values.empty();
      entry.trailing_text_ =
          std::string(value_text.substr(parsed.last_end));
      entry.interior_comments_ = comments_before(value_text, parsed.last_end);
      entry.values_.reserve(parsed.values.size());
      for (const auto& parsed_value : parsed.values) {
        auto value = make_value(parsed_value);
        value.original_text_ = parsed_value.original_text;
        entry.values_.push_back(std::move(value));
      }
      entry.original_values_ = entry.values_;
      section.entries_.push_back(std::move(entry));
    }
    sections_.push_back(std::move(section));
    cursor = ending->end;
  }

  outside_text_.push_back(bytes.substr(cursor));
  if (!outside_text_.back().empty()) {
    raw_sections_.push_back(
        RawSection{"outside_" + std::to_string(outside_text_.size() - 1),
                   outside_text_.back(), cursor, false});
  }
  if (sections_.empty()) {
    parse_failure("no Fortran namelist section found", filename_, bytes, 0);
  }
  rebuild_fields();
}

void KFile::rebuild_fields() {
  fields_.clear();
  field_targets_.clear();

  // Determine the effective (last) assignment for every name before expanding
  // repetitions.  This prevents a chain of duplicate assignments from
  // repeatedly allocating large temporary vectors that are immediately
  // discarded.  The ordered section/entry model remains untouched.
  std::vector<std::pair<std::string, FieldTarget>> effective_targets;
  std::unordered_map<std::string, std::size_t> effective_indices;
  for (std::size_t section_index = 0; section_index < sections_.size();
       ++section_index) {
    const auto& section = sections_[section_index];
    for (std::size_t entry_index = 0; entry_index < section.entries_.size();
         ++entry_index) {
      const auto& name = section.entries_[entry_index].name();
      const auto [position, inserted] =
          effective_indices.emplace(name, effective_targets.size());
      if (inserted) {
        effective_targets.push_back(
            {name, FieldTarget{section_index, entry_index}});
      } else {
        effective_targets[position->second].second =
            FieldTarget{section_index, entry_index};
      }
    }
  }

  std::size_t expanded_total = 0;
  std::size_t string_bytes_total = 0;
  for (const auto& item : effective_targets) {
    const auto& target = item.second;
    const auto& entry = sections_[target.section].entries_[target.entry];
    const auto available_values = kMaximumExpandedValues - expanded_total;
    const auto available_string_bytes =
        kMaximumExpandedStringBytes - string_bytes_total;
    auto value = field_from_entry(entry, available_values,
                                  available_string_bytes);
    if (!value) {
      continue;
    }
    expanded_total += field_element_count(*value);
    string_bytes_total += field_string_storage_bytes(*value);
    fields_.insert(item.first, std::move(*value), false, fields_.size());
    field_targets_.push_back(item);
  }
}

void KFile::refresh_field(const std::string& name) {
  const auto normalized = ascii_upper(name);
  FieldTarget target;
  bool found = false;
  std::size_t other_expanded = 0;
  std::size_t other_string_bytes = 0;
  for (const auto& field : fields_) {
    if (field.name != normalized) {
      const auto count = field_element_count(field.value);
      if (count > kMaximumExpandedValues - other_expanded) {
        other_expanded = kMaximumExpandedValues;
      } else {
        other_expanded += count;
      }
      const auto string_bytes = field_string_storage_bytes(field.value);
      if (string_bytes >
          kMaximumExpandedStringBytes - other_string_bytes) {
        other_string_bytes = kMaximumExpandedStringBytes;
      } else {
        other_string_bytes += string_bytes;
      }
    }
  }
  const auto available_values = kMaximumExpandedValues - other_expanded;
  const auto available_string_bytes =
      kMaximumExpandedStringBytes - other_string_bytes;
  const NamelistEntry* effective_entry = nullptr;
  for (std::size_t section_index = 0; section_index < sections_.size();
       ++section_index) {
    const auto& section = sections_[section_index];
    for (std::size_t entry_index = 0; entry_index < section.entries_.size();
         ++entry_index) {
      const auto& item = section.entries_[entry_index];
      if (item.name() != normalized) {
        continue;
      }
      found = true;
      effective_entry = &item;
      target = FieldTarget{section_index, entry_index};
    }
  }
  auto value = found ? field_from_entry(*effective_entry, available_values,
                                        available_string_bytes)
                     : std::optional<FieldValue>{};

  auto old_target = std::find_if(
      field_targets_.begin(), field_targets_.end(),
      [&](const auto& item) { return item.first == normalized; });
  if (!found || !value) {
    fields_.erase(normalized);
    if (old_target != field_targets_.end()) {
      field_targets_.erase(old_target);
    }
    return;
  }
  if (fields_.contains(normalized)) {
    auto& current = fields_.at(normalized);
    if (!assign_preserving_array_storage(current, *value)) {
      fields_.set(normalized, std::move(*value));
    }
  } else {
    fields_.insert(normalized, std::move(*value), false, fields_.size());
  }
  if (old_target == field_targets_.end()) {
    field_targets_.push_back({normalized, target});
  } else {
    old_target->second = target;
  }
}

std::size_t KFile::section_count(const std::string& name) const {
  const auto normalized = ascii_upper(name);
  return static_cast<std::size_t>(std::count_if(
      sections_.begin(), sections_.end(), [&](const NamelistSection& item) {
        return item.name() == normalized;
      }));
}

const NamelistSection& KFile::section(const std::string& name,
                                      std::size_t occurrence) const {
  const auto normalized = ascii_upper(name);
  for (const auto& item : sections_) {
    if (item.name() == normalized) {
      if (occurrence == 0) {
        return item;
      }
      --occurrence;
    }
  }
  throw FieldError("unknown namelist section: " + normalized);
}

const NamelistEntry& KFile::entry(const std::string& section_name,
                                  const std::string& name,
                                  std::size_t occurrence,
                                  std::size_t section_occurrence) const {
  return section(section_name, section_occurrence).entry(name, occurrence);
}

void KFile::set(const std::string& section_name, const std::string& name,
                std::vector<NamelistValue> values,
                std::size_t occurrence,
                std::size_t section_occurrence) {
  const auto normalized_section = ascii_upper(section_name);
  const auto normalized_name = ascii_upper(name);
  for (auto& section_item : sections_) {
    if (section_item.name() != normalized_section) {
      continue;
    }
    if (section_occurrence != 0) {
      --section_occurrence;
      continue;
    }
    for (auto& entry_item : section_item.entries_) {
      if (entry_item.name() == normalized_name) {
        if (occurrence == 0) {
          entry_item.values_ = std::move(values);
          entry_item.parsed_ = true;
          entry_item.explicitly_modified_ = true;
          refresh_field(normalized_name);
          return;
        }
        --occurrence;
      }
    }
    throw FieldError("unknown namelist entry: " + normalized_name);
  }
  throw FieldError("unknown namelist section: " + normalized_section);
}

bool KFile::contains(const std::string& name) const {
  return fields_.contains(ascii_upper(name));
}

FieldValue& KFile::at(const std::string& name) {
  return fields_.at(ascii_upper(name));
}

const FieldValue& KFile::at(const std::string& name) const {
  return fields_.at(ascii_upper(name));
}

std::string KFile::serialize_section(std::size_t section_index) const {
  const auto& section_item = sections_.at(section_index);
  std::string output;
  std::size_t previous = 0;

  for (std::size_t entry_index = 0;
       entry_index < section_item.entries_.size(); ++entry_index) {
    const auto& entry_item = section_item.entries_[entry_index];
    if (entry_item.relative_begin_ < previous ||
        entry_item.relative_end_ < entry_item.relative_begin_ ||
        entry_item.relative_end_ > section_item.raw_text_.size()) {
      throw ValidationError("invalid stored K-file source ranges");
    }
    output.append(section_item.raw_text_, previous,
                  entry_item.relative_begin_ - previous);

    const std::vector<NamelistValue>* replacement = nullptr;
    std::vector<NamelistValue> field_replacement;
    const auto target = std::find_if(
        field_targets_.begin(), field_targets_.end(), [&](const auto& item) {
          return item.second.section == section_index &&
                 item.second.entry == entry_index;
        });
    if (target != field_targets_.end() && fields_.contains(target->first)) {
      const auto& current = fields_.at(target->first);
      const auto entry_field = field_from_entry(entry_item);
      if (!entry_field || !field_values_equal(current, *entry_field)) {
        field_replacement = values_from_field(current);
        replacement = &field_replacement;
      }
    }
    if (replacement == nullptr && entry_item.modified()) {
      replacement = &entry_item.values_;
    }

    if (replacement == nullptr) {
      output += entry_item.raw_text_;
    } else {
      output += entry_item.name_;
      if (!entry_item.subscript_.empty()) {
        output.push_back('(');
        output += entry_item.subscript_;
        output.push_back(')');
      }
      output += " = ";
      for (std::size_t value_index = 0; value_index < replacement->size();
           ++value_index) {
        if (value_index != 0) {
          output += ", ";
        }
        output += serialize_value((*replacement)[value_index]);
      }
      if (!entry_item.interior_comments_.empty()) {
        output.push_back('\n');
        output += entry_item.interior_comments_;
      }
      output += entry_item.trailing_text_;
    }
    previous = entry_item.relative_end_;
  }
  output.append(section_item.raw_text_, previous, std::string::npos);
  return output;
}

void KFile::write(const std::filesystem::path& path) const {
  for (const auto& field : fields_) {
    const auto target = std::find_if(
        field_targets_.begin(), field_targets_.end(),
        [&](const auto& item) { return item.first == field.name; });
    if (target == field_targets_.end()) {
      throw ValidationError("K-file field has no namelist entry: " +
                            field.name);
    }
  }
  if (outside_text_.size() != sections_.size() + 1) {
    throw ValidationError("invalid stored K-file document structure");
  }
  std::string output;
  for (std::size_t index = 0; index < sections_.size(); ++index) {
    output += outside_text_[index];
    output += serialize_section(index);
  }
  output += outside_text_.back();
  detail::write_binary_file(path, output);
}

}  // namespace eqmdsk
