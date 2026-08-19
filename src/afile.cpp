#include "eqmdsk/afile.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "detail/fortran.hpp"
#include "eqmdsk/error.hpp"

namespace eqmdsk {
namespace {

struct Line {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::size_t number = 0;
  std::string_view text;
  std::string ending;
};

constexpr std::array<std::array<const char*, 4>, 6> kInitialRecords{{
    {{"CHISQ", "RCENCM", "BCENTR", "IPMEAS"}},
    {{"IPMHD", "RCNTR", "ZCNTR", "AMINOR"}},
    {{"ELONG", "UTRI", "LTRI", "VOLUME"}},
    {{"RCURRT", "ZCURRT", "QSTAR", "BETAT"}},
    {{"BETAP", "LI", "GAPIN", "GAPOUT"}},
    {{"GAPTOP", "GAPBOT", "Q95", "VERTN"}},
}};

constexpr std::array<std::array<const char*, 4>, 11> kLaterRecords{{
    {{"SHEAR", "BPOLAV", "S1", "S2"}},
    {{"S3", "QOUT", "SEPIN", "SEPOUT"}},
    {{"SEPTOP", "SIBDRY", "AREA", "WMHD"}},
    {{"ERROR", "ELONGM", "QM", "CDFLUX"}},
    {{"ALPHA", "RTTT", "PSIREF", "INDENT"}},
    // Record 5 is represented by the RSEPS and ZSEPS vectors.
    {{nullptr, nullptr, nullptr, nullptr}},
    {{"SEPEXP", "SEPBOT", "BTAXP", "BTAXV"}},
    {{"AQ1", "AQ2", "AQ3", "DSEP"}},
    {{"RM", "ZM", "PSIM", "TAUMHD"}},
    {{"BETAPD", "BETATD", "WDIA", "DIAMAG"}},
    {{"VLOOP", "TAUDIA", "QMERCI", "TAVEM"}},
}};

constexpr std::array<std::array<const char*, 4>, 15> kOptionalRecords{{
    {{"PBINJ", "RVSIN", "ZVSIN", "RVSOUT"}},
    {{"ZVSOUT", "VSURF", "WPDOT", "WBDOT"}},
    {{"SLANTU", "SLANTL", "ZUPERTS", "CHIPRE"}},
    {{"CJOR95", "PP95", "DRSEP", "YYY2"}},
    {{"XNNC", "CPROF", "ORING", "CJOR0"}},
    {{"FEXPAN", "QMIN", "CHIMSE", "SSI01"}},
    {{"FEXPVS", "SEPNOSE", "SSI95", "RHOQMIN"}},
    {{"CJOR99", "CJ1AVE", "RMIDIN", "RMIDOUT"}},
    {{"PSURFA", "PEAK", "DMINUX", "DMINLX"}},
    {{"DOLUBAF", "DOLUBAFM", "DILUDOM", "DILUDOMM"}},
    {{"RATSOL", "RVSIU", "ZVSIU", "RVSID"}},
    {{"ZVSID", "RVSOU", "ZVSOU", "RVSOD"}},
    {{"ZVSOD", "CONDNO", "PSIN32", "PSIN21"}},
    {{"RQ32IN", "RQ21TOP", "CHILIBT", "LI3"}},
    {{"XBETAPR", "TFLUX", "TCHIMLS", "TWAGAP"}},
}};

constexpr std::array<const char*, 4> kChordArrays{{
    "RCO2V", "DCO2V", "RCO2R", "DCO2R"}};
constexpr std::array<const char*, 4> kResponseArrays{{
    "CSILOP", "CMPR2", "CCBRSP", "ECCURT"}};

std::vector<Line> split_lines(const std::string& bytes) {
  std::vector<Line> result;
  std::size_t offset = 0;
  std::size_t number = 1;
  while (offset < bytes.size()) {
    const auto newline = bytes.find('\n', offset);
    const auto raw_end = newline == std::string::npos ? bytes.size() : newline;
    auto content_end = raw_end;
    std::string ending;
    if (content_end > offset && bytes[content_end - 1] == '\r') {
      --content_end;
      ending = newline == std::string::npos ? "\r" : "\r\n";
    } else if (newline != std::string::npos) {
      ending = "\n";
    }
    const auto end = newline == std::string::npos ? bytes.size() : newline + 1;
    result.push_back(Line{offset, end, number,
                          std::string_view(bytes.data() + offset,
                                           content_end - offset),
                          std::move(ending)});
    offset = end;
    ++number;
  }
  return result;
}

std::optional<std::int64_t> parse_integer(std::string_view text) {
  auto trimmed = detail::trim_copy(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  if (trimmed.front() == '+') {
    trimmed.erase(trimmed.begin());
  }
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::int64_t value = 0;
  const auto result =
      std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
  if (result.ec != std::errc{} ||
      result.ptr != trimmed.data() + trimmed.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<double> parse_real(std::string_view text) {
  double value = 0.0;
  if (!detail::parse_fortran_real(text, value)) {
    return std::nullopt;
  }
  return value;
}

bool try_real_record(const Line& line, std::size_t count,
                     const std::string& filename,
                     std::vector<double>& values) {
  values.clear();
  values.reserve(count);
  const std::string input(line.text);
  try {
    detail::NumericCursor cursor(input, 0, filename);
    for (std::size_t i = 0; i < count; ++i) {
      const auto value = cursor.next_real("A-file record");
      if (!std::isfinite(value)) {
        return false;
      }
      values.push_back(value);
    }
    auto position = cursor.position();
    while (position < input.size() &&
           (input[position] == ' ' || input[position] == '\t' ||
            input[position] == ',')) {
      ++position;
    }
    return position == input.size();
  } catch (const ParseError&) {
    values.clear();
    return false;
  }
}

std::array<double, 4> require_real_record(const Line& line,
                                          const std::string& filename,
                                          const std::string& context) {
  std::vector<double> values;
  if (!try_real_record(line, 4, filename, values)) {
    throw ParseError("expected a four-real A-file record for " + context,
                     filename, line.number, 1);
  }
  return {{values[0], values[1], values[2], values[3]}};
}

std::array<std::int64_t, 4> require_integer_record(
    const Line& line, const std::string& filename) {
  std::array<std::int64_t, 4> result{};
  const auto text = line.text;
  bool fixed_ok = text.size() >= 21;
  if (fixed_ok) {
    for (std::size_t index = 0; index < result.size(); ++index) {
      const auto value = parse_integer(text.substr(1 + index * 5, 5));
      if (!value) {
        fixed_ok = false;
        break;
      }
      result[index] = *value;
    }
    for (std::size_t index = 21; fixed_ok && index < text.size(); ++index) {
      if (text[index] != ' ' && text[index] != '\t') {
        fixed_ok = false;
      }
    }
    if (fixed_ok) {
      return result;
    }
  }

  const std::string input(text);
  try {
    detail::NumericCursor cursor(input, 0, filename);
    for (std::size_t index = 0; index < result.size(); ++index) {
      result[index] = cursor.next_integer("A-file count record");
    }
    auto position = cursor.position();
    while (position < input.size() &&
           (input[position] == ' ' || input[position] == '\t' ||
            input[position] == ',')) {
      ++position;
    }
    if (position == input.size()) {
      return result;
    }
  } catch (const ParseError&) {
  }
  throw ParseError("expected a four-integer A-file count record", filename,
                   line.number, 1);
}

template <typename T>
const T& require(const FieldMap& fields, const char* name) {
  const auto& value = fields.at(name);
  if (!std::holds_alternative<T>(value)) {
    throw ValidationError(std::string(name) + " has type " +
                          field_type_name(value) + ", expected a different type");
  }
  return std::get<T>(value);
}

DoubleVector to_vector(const std::vector<double>& values) {
  DoubleVector result(static_cast<Eigen::Index>(values.size()));
  for (std::size_t index = 0; index < values.size(); ++index) {
    result[static_cast<Eigen::Index>(index)] = values[index];
  }
  return result;
}

std::size_t record_count(std::size_t value_count) noexcept {
  return value_count / 4 + (value_count % 4 != 0 ? 1 : 0);
}

std::string integer_field(std::int64_t value, int width,
                          const std::string& name) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setw(width) << value;
  const auto text = output.str();
  if (text.size() > static_cast<std::size_t>(width)) {
    throw ValidationError(name + " does not fit I" + std::to_string(width));
  }
  return text;
}

std::string time_field(double value) {
  if (!std::isfinite(value)) {
    throw ValidationError("TIME must be finite");
  }
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::fixed << std::setprecision(3) << std::setw(8) << value;
  const auto text = output.str();
  if (text.size() > 8) {
    throw ValidationError("TIME does not fit F8.3");
  }
  return text;
}

std::string real_field(double value, const std::string& name) {
  try {
    return detail::format_e16_9(value);
  } catch (const ValidationError&) {
    throw ValidationError(name + " does not fit E16.9");
  }
}

void append_real_record(std::string& output,
                        const std::vector<std::pair<double, std::string>>& values,
                        const std::string& line_ending) {
  if (values.empty() || values.size() > 4) {
    throw ValidationError("an A-file real record must contain 1..4 values");
  }
  output += ' ';
  for (const auto& [value, name] : values) {
    output += real_field(value, name);
  }
  output += line_ending;
}

void append_named_record(std::string& output, const FieldMap& fields,
                         const std::array<const char*, 4>& names,
                         const std::string& line_ending) {
  std::vector<std::pair<double, std::string>> values;
  values.reserve(4);
  for (const auto* name : names) {
    values.emplace_back(require<double>(fields, name), name);
  }
  append_real_record(output, values, line_ending);
}

void append_vector_records(std::string& output, const DoubleVector& values,
                           const std::string& name,
                           const std::string& line_ending) {
  for (Eigen::Index offset = 0; offset < values.size(); offset += 4) {
    std::vector<std::pair<double, std::string>> record;
    const auto end = std::min<Eigen::Index>(values.size(), offset + 4);
    for (auto index = offset; index < end; ++index) {
      record.emplace_back(values[index], name);
    }
    append_real_record(output, record, line_ending);
  }
}

bool all_fields_present(const FieldMap& fields,
                        const std::array<const char*, 4>& names) {
  return std::all_of(names.begin(), names.end(), [&](const char* name) {
    return fields.contains(name);
  });
}

bool any_fields_present(const FieldMap& fields,
                        const std::array<const char*, 4>& names) {
  return std::any_of(names.begin(), names.end(), [&](const char* name) {
    return fields.contains(name);
  });
}

}  // namespace

AFile::AFile(const std::filesystem::path& filename) : EFITFile(filename) {
  parse(detail::read_binary_file(filename));
}

void AFile::parse(const std::string& bytes) {
  const auto diagnostic_filename = detail::path_for_diagnostic(filename_);
  const auto lines = split_lines(bytes);
  if (lines.empty()) {
    throw ParseError("empty A-file", diagnostic_filename, 1, 1);
  }

  std::size_t control_index = lines.size();
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (!lines[index].text.empty() && lines[index].text.front() == '*') {
      control_index = index;
      break;
    }
  }
  if (control_index == lines.size()) {
    throw ParseError("unable to locate the '*' A-file control record",
                     diagnostic_filename, 1, 1);
  }
  if (control_index < 2) {
    throw ParseError("A-file header must contain date and shot records",
                     diagnostic_filename, lines[control_index].number, 1);
  }

  header_ = bytes.substr(0, lines[control_index].begin);
  const auto& shot_line = lines[1];
  if (shot_line.text.size() < 7) {
    throw ParseError("A-file shot record is shorter than I7",
                     diagnostic_filename, shot_line.number, 1);
  }
  const auto shot = parse_integer(shot_line.text.substr(0, 7));
  if (!shot) {
    throw ParseError("invalid SHOT in A-file header", diagnostic_filename,
                     shot_line.number, 1);
  }
  shot_field_offset_ = shot_line.begin;
  time_field_offset_ = std::string::npos;
  std::optional<double> header_time;
  if (control_index >= 3) {
    const auto& header_time_line = lines[2];
    if (header_time_line.text.size() >= 17) {
      header_time = parse_real(header_time_line.text.substr(1, 16));
      if (header_time) {
        time_field_offset_ = header_time_line.begin + 1;
      }
    }
  }

  const auto& control = lines[control_index];
  std::int64_t jflag = 0;
  std::int64_t lflag = 0;
  std::int64_t mco2v_raw = 0;
  std::int64_t mco2r_raw = 0;
  std::int64_t nlold = 0;
  std::int64_t nlnew = 0;
  double time = 0.0;
  std::string limloc;
  std::string qmflag;

  bool fixed_control = control.text.size() >= 66;
  if (fixed_control) {
    const auto parsed_time = parse_real(control.text.substr(1, 8));
    const auto parsed_jflag = parse_integer(control.text.substr(18, 5));
    const auto parsed_lflag = parse_integer(control.text.substr(34, 5));
    const auto parsed_mco2v = parse_integer(control.text.substr(44, 3));
    const auto parsed_mco2r = parse_integer(control.text.substr(48, 3));
    auto parsed_nlold = parse_integer(control.text.substr(56, 5));
    auto parsed_nlnew = parse_integer(control.text.substr(61, 5));
    // Blank old/new limiter counts are a defined historical representation
    // of zero and are accepted without weakening the other control fields.
    if (!parsed_nlold && detail::trim_copy(control.text.substr(56, 5)).empty()) {
      parsed_nlold = 0;
    }
    if (!parsed_nlnew && detail::trim_copy(control.text.substr(61, 5)).empty()) {
      parsed_nlnew = 0;
    }
    fixed_control = parsed_jflag && parsed_lflag && parsed_mco2v &&
                    parsed_mco2r && parsed_nlold && parsed_nlnew;
    if (fixed_control) {
      // TIME is redundantly stored in the standard third header record.  Some
      // historical writers damaged only the control-record copy, so recover
      // from the header rather than rejecting otherwise usable data.
      time = parsed_time.value_or(header_time.value_or(0.0));
      jflag = *parsed_jflag;
      lflag = *parsed_lflag;
      limloc = detail::trim_copy(control.text.substr(40, 3));
      mco2v_raw = *parsed_mco2v;
      mco2r_raw = *parsed_mco2r;
      qmflag = detail::trim_copy(control.text.substr(52, 3));
      nlold = *parsed_nlold;
      nlnew = *parsed_nlnew;
      control_suffix_ = std::string(control.text.substr(66));
    }
  }
  if (!fixed_control) {
    std::istringstream stream(std::string(control.text.substr(1)));
    stream.imbue(std::locale::classic());
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
      tokens.push_back(std::move(token));
    }
    if (tokens.size() != 8 && tokens.size() != 9) {
      throw ParseError("invalid A-file control record", diagnostic_filename,
                       control.number, 1);
    }
    const bool has_time_token = tokens.size() == 9;
    const std::size_t base = has_time_token ? 1 : 0;
    const auto parsed_time =
        has_time_token ? parse_real(tokens[0]) : std::optional<double>{};
    const auto parsed_jflag = parse_integer(tokens[base]);
    const auto parsed_lflag = parse_integer(tokens[base + 1]);
    limloc = tokens[base + 2];
    const auto parsed_mco2v = parse_integer(tokens[base + 3]);
    const auto parsed_mco2r = parse_integer(tokens[base + 4]);
    qmflag = tokens[base + 5];
    const auto parsed_nlold = parse_integer(tokens[base + 6]);
    const auto parsed_nlnew = parse_integer(tokens[base + 7]);
    if (!parsed_jflag || !parsed_lflag || !parsed_mco2v || !parsed_mco2r ||
        !parsed_nlold || !parsed_nlnew ||
        limloc.size() > 3 || qmflag.size() > 3) {
      throw ParseError("invalid value in A-file control record",
                       diagnostic_filename, control.number, 1);
    }
    time = parsed_time.value_or(header_time.value_or(0.0));
    jflag = *parsed_jflag;
    lflag = *parsed_lflag;
    mco2v_raw = *parsed_mco2v;
    mco2r_raw = *parsed_mco2r;
    nlold = *parsed_nlold;
    nlnew = *parsed_nlnew;
  }

  std::size_t mco2v = 0;
  std::size_t mco2r = 0;
  try {
    mco2v = detail::checked_count(mco2v_raw, "MCO2V");
    mco2r = detail::checked_count(mco2r_raw, "MCO2R");
  } catch (const ValidationError& error) {
    throw ParseError(error.what(), diagnostic_filename, control.number, 1);
  }
  if (mco2v > bytes.size() || mco2r > bytes.size()) {
    throw ParseError("A-file chord count exceeds available data",
                     diagnostic_filename, control.number, 1);
  }

  control_line_ending_ = control.ending.empty() ? "\n" : control.ending;
  record_line_ending_ = control_line_ending_;
  std::size_t order = 0;
  fields_.insert("SHOT", *shot, true, order++);
  fields_.insert("TIME", time, true, order++);
  fields_.insert("JFLAG", jflag, true, order++);
  fields_.insert("LFLAG", lflag, true, order++);
  fields_.insert("LIMLOC", limloc, true, order++);
  fields_.insert("MCO2V", mco2v_raw, true, order++);
  fields_.insert("MCO2R", mco2r_raw, true, order++);
  fields_.insert("QMFLAG", qmflag, true, order++);
  fields_.insert("NLOLD", nlold, true, order++);
  fields_.insert("NLNEW", nlnew, true, order++);

  std::size_t line_index = control_index + 1;
  auto next_line = [&](const std::string& context) -> const Line& {
    if (line_index >= lines.size()) {
      throw ParseError("unexpected end of file while reading " + context,
                       diagnostic_filename,
                       lines.empty() ? 1 : lines.back().number, 1);
    }
    return lines[line_index++];
  };

  for (const auto& names : kInitialRecords) {
    const auto values = require_real_record(next_line(names[0]),
                                            diagnostic_filename, names[0]);
    for (std::size_t index = 0; index < 4; ++index) {
      fields_.insert(names[index], values[index], true, order++);
    }
  }

  auto read_vector = [&](std::size_t count, const char* name) {
    std::vector<double> values;
    values.reserve(count);
    for (std::size_t record = 0; record < record_count(count); ++record) {
      const auto expected = std::min<std::size_t>(4, count - values.size());
      const auto& line = next_line(name);
      std::vector<double> chunk;
      bool parsed = false;
      if (expected < 4 &&
          try_real_record(line, 4, diagnostic_filename, chunk)) {
        chunk.resize(expected);
        parsed = true;
      } else {
        parsed = try_real_record(line, expected, diagnostic_filename, chunk);
      }
      if (!parsed) {
        throw ParseError("invalid A-file array record for " +
                             std::string(name),
                         diagnostic_filename, line.number, 1);
      }
      values.insert(values.end(), chunk.begin(), chunk.end());
    }
    fields_.insert(name, to_vector(values), true, order++);
  };
  read_vector(mco2v, "RCO2V");
  read_vector(mco2v, "DCO2V");
  read_vector(mco2r, "RCO2R");
  read_vector(mco2r, "DCO2R");

  for (std::size_t record = 0; record < kLaterRecords.size(); ++record) {
    const auto values = require_real_record(
        next_line(record == 5 ? "RSEPS" : kLaterRecords[record][0]),
        diagnostic_filename,
        record == 5 ? "RSEPS/ZSEPS" : kLaterRecords[record][0]);
    if (record == 5) {
      DoubleVector rseps(2);
      DoubleVector zseps(2);
      rseps << values[0], values[2];
      zseps << values[1], values[3];
      fields_.insert("RSEPS", std::move(rseps), true, order++);
      fields_.insert("ZSEPS", std::move(zseps), true, order++);
    } else {
      for (std::size_t index = 0; index < 4; ++index) {
        fields_.insert(kLaterRecords[record][index], values[index], true,
                       order++);
      }
    }
  }

  const auto& count_line = next_line("A-file response counts");
  const auto counts = require_integer_record(count_line, diagnostic_filename);
  std::array<std::size_t, 4> checked_counts{};
  for (std::size_t index = 0; index < counts.size(); ++index) {
    try {
      checked_counts[index] = detail::checked_count(counts[index],
                                                    kResponseArrays[index]);
    } catch (const ValidationError& error) {
      throw ParseError(error.what(), diagnostic_filename, count_line.number, 1);
    }
    if (checked_counts[index] > bytes.size()) {
      throw ParseError("A-file response count exceeds available data",
                       diagnostic_filename, count_line.number, 1);
    }
  }
  if (checked_counts[0] >
      std::numeric_limits<std::size_t>::max() - checked_counts[1]) {
    throw ParseError("combined A-file response count overflows size_t",
                     diagnostic_filename, count_line.number, 1);
  }
  fields_.insert("NSILOP0", counts[0], true, order++);
  fields_.insert("MAGPRI0", counts[1], true, order++);
  fields_.insert("NFCOIL0", counts[2], true, order++);
  fields_.insert("NESUM0", counts[3], true, order++);

  const auto combined_count = checked_counts[0] + checked_counts[1];
  std::vector<double> combined;
  combined.reserve(combined_count);
  for (std::size_t record = 0; record < record_count(combined_count); ++record) {
    const auto expected =
        std::min<std::size_t>(4, combined_count - combined.size());
    const auto& line = next_line("CSILOP/CMPR2");
    std::vector<double> chunk;
    bool parsed = false;
    if (expected < 4 &&
        try_real_record(line, 4, diagnostic_filename, chunk)) {
      chunk.resize(expected);
      parsed = true;
    } else {
      parsed = try_real_record(line, expected, diagnostic_filename, chunk);
    }
    if (!parsed) {
      throw ParseError("invalid combined CSILOP/CMPR2 record",
                       diagnostic_filename, line.number, 1);
    }
    combined.insert(combined.end(), chunk.begin(), chunk.end());
  }
  fields_.insert("CSILOP",
                 to_vector(std::vector<double>(combined.begin(),
                                               combined.begin() +
                                                   checked_counts[0])),
                 true, order++);
  fields_.insert("CMPR2",
                 to_vector(std::vector<double>(combined.begin() +
                                                   checked_counts[0],
                                               combined.end())),
                 true, order++);
  read_vector(checked_counts[2], "CCBRSP");
  read_vector(checked_counts[3], "ECCURT");

  for (const auto& names : kOptionalRecords) {
    if (line_index >= lines.size()) {
      break;
    }
    std::vector<double> values;
    if (!try_real_record(lines[line_index], 4, diagnostic_filename, values)) {
      break;
    }
    ++line_index;
    for (std::size_t index = 0; index < 4; ++index) {
      fields_.insert(names[index], values[index], true, order++);
    }
  }

  const auto footer_offset =
      line_index < lines.size() ? lines[line_index].begin : bytes.size();
  footer_ = bytes.substr(footer_offset);
  raw_sections_.push_back(RawSection{"header", header_, 0, false});
  if (!control_suffix_.empty()) {
    raw_sections_.push_back(RawSection{
        "control_suffix", control_suffix_, control.begin + 66, false});
  }
  if (!footer_.empty()) {
    raw_sections_.push_back(
        RawSection{"footer", footer_, footer_offset, false});
  }
}

std::size_t AFile::optional_record_count() const noexcept {
  std::size_t count = 0;
  for (const auto& names : kOptionalRecords) {
    if (!all_fields_present(fields_, names)) {
      break;
    }
    ++count;
  }
  return count;
}

void AFile::validate_for_write() const {
  static constexpr std::array<const char*, 8> integer_fields{{
      "SHOT", "JFLAG", "LFLAG", "MCO2V", "MCO2R", "NLOLD", "NLNEW",
      "NSILOP0"}};
  for (const auto* name : integer_fields) {
    static_cast<void>(require<std::int64_t>(fields_, name));
  }
  for (const auto* name : {"MAGPRI0", "NFCOIL0", "NESUM0"}) {
    static_cast<void>(require<std::int64_t>(fields_, name));
  }
  static_cast<void>(require<double>(fields_, "TIME"));
  const auto& limloc = require<std::string>(fields_, "LIMLOC");
  const auto& qmflag = require<std::string>(fields_, "QMFLAG");
  if (limloc.size() > 3 || qmflag.size() > 3) {
    throw ValidationError("LIMLOC and QMFLAG must contain at most 3 bytes");
  }
  const auto printable_ascii = [](const std::string& value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
      return character >= 0x20 && character <= 0x7e;
    });
  };
  if (!printable_ascii(limloc) || !printable_ascii(qmflag)) {
    throw ValidationError("LIMLOC and QMFLAG must contain printable ASCII");
  }

  static_cast<void>(integer_field(require<std::int64_t>(fields_, "SHOT"), 7,
                                  "SHOT"));
  static_cast<void>(time_field(require<double>(fields_, "TIME")));
  static_cast<void>(integer_field(require<std::int64_t>(fields_, "JFLAG"), 5,
                                  "JFLAG"));
  static_cast<void>(integer_field(require<std::int64_t>(fields_, "LFLAG"), 5,
                                  "LFLAG"));
  static_cast<void>(integer_field(require<std::int64_t>(fields_, "MCO2V"), 3,
                                  "MCO2V"));
  static_cast<void>(integer_field(require<std::int64_t>(fields_, "MCO2R"), 3,
                                  "MCO2R"));
  static_cast<void>(integer_field(require<std::int64_t>(fields_, "NLOLD"), 5,
                                  "NLOLD"));
  static_cast<void>(integer_field(require<std::int64_t>(fields_, "NLNEW"), 5,
                                  "NLNEW"));

  for (const auto& names : kInitialRecords) {
    for (const auto* name : names) {
      static_cast<void>(real_field(require<double>(fields_, name), name));
    }
  }
  for (std::size_t record = 0; record < kLaterRecords.size(); ++record) {
    if (record == 5) {
      continue;
    }
    for (const auto* name : kLaterRecords[record]) {
      static_cast<void>(real_field(require<double>(fields_, name), name));
    }
  }
  for (const auto* name : {"RSEPS", "ZSEPS"}) {
    const auto& values = require<DoubleVector>(fields_, name);
    if (values.size() != 2) {
      throw ValidationError(std::string(name) + " length must equal 2");
    }
    for (Eigen::Index index = 0; index < values.size(); ++index) {
      static_cast<void>(real_field(values[index], name));
    }
  }

  const auto mco2v = detail::checked_count(
      require<std::int64_t>(fields_, "MCO2V"), "MCO2V");
  const auto mco2r = detail::checked_count(
      require<std::int64_t>(fields_, "MCO2R"), "MCO2R");
  const std::array<std::size_t, 4> chord_counts{{mco2v, mco2v, mco2r,
                                                 mco2r}};
  for (std::size_t index = 0; index < kChordArrays.size(); ++index) {
    const auto& values = require<DoubleVector>(fields_, kChordArrays[index]);
    if (static_cast<std::size_t>(values.size()) != chord_counts[index]) {
      throw ValidationError(std::string(kChordArrays[index]) +
                            " length does not match its control count");
    }
    for (Eigen::Index item = 0; item < values.size(); ++item) {
      static_cast<void>(real_field(values[item], kChordArrays[index]));
    }
  }

  const std::array<const char*, 4> count_names{{
      "NSILOP0", "MAGPRI0", "NFCOIL0", "NESUM0"}};
  for (std::size_t index = 0; index < count_names.size(); ++index) {
    const auto count = detail::checked_count(
        require<std::int64_t>(fields_, count_names[index]), count_names[index]);
    static_cast<void>(integer_field(static_cast<std::int64_t>(count), 5,
                                    count_names[index]));
    const auto& values = require<DoubleVector>(fields_, kResponseArrays[index]);
    if (static_cast<std::size_t>(values.size()) != count) {
      throw ValidationError(std::string(kResponseArrays[index]) +
                            " length does not match " + count_names[index]);
    }
    for (Eigen::Index item = 0; item < values.size(); ++item) {
      static_cast<void>(real_field(values[item], kResponseArrays[index]));
    }
  }

  bool found_absent = false;
  for (const auto& names : kOptionalRecords) {
    const auto any = any_fields_present(fields_, names);
    const auto all = all_fields_present(fields_, names);
    if (any && !all) {
      throw ValidationError(
          "an optional A-file record must contain all four fields");
    }
    if (all && found_absent) {
      throw ValidationError("optional A-file records must be contiguous");
    }
    if (!all) {
      found_absent = true;
      continue;
    }
    for (const auto* name : names) {
      static_cast<void>(real_field(require<double>(fields_, name), name));
    }
  }
}

void AFile::write(const std::filesystem::path& path) const {
  validate_for_write();

  std::string output = header_;
  if (shot_field_offset_ + 7 > output.size()) {
    throw ValidationError("stored A-file header offsets are invalid");
  }
  output.replace(shot_field_offset_, 7,
                 integer_field(require<std::int64_t>(fields_, "SHOT"), 7,
                               "SHOT"));
  if (time_field_offset_ != std::string::npos) {
    if (time_field_offset_ + 16 > output.size()) {
      throw ValidationError("stored A-file TIME header offset is invalid");
    }
    output.replace(time_field_offset_, 16,
                   real_field(require<double>(fields_, "TIME"), "TIME"));
  }

  output += '*';
  output += time_field(require<double>(fields_, "TIME"));
  output.append(9, ' ');
  output += integer_field(require<std::int64_t>(fields_, "JFLAG"), 5,
                          "JFLAG");
  output.append(11, ' ');
  output += integer_field(require<std::int64_t>(fields_, "LFLAG"), 5,
                          "LFLAG");
  output += ' ';
  auto limloc = require<std::string>(fields_, "LIMLOC");
  limloc.append(3 - limloc.size(), ' ');
  output += limloc;
  output += ' ';
  output += integer_field(require<std::int64_t>(fields_, "MCO2V"), 3,
                          "MCO2V");
  output += ' ';
  output += integer_field(require<std::int64_t>(fields_, "MCO2R"), 3,
                          "MCO2R");
  output += ' ';
  auto qmflag = require<std::string>(fields_, "QMFLAG");
  qmflag.append(3 - qmflag.size(), ' ');
  output += qmflag;
  output += ' ';
  output += integer_field(require<std::int64_t>(fields_, "NLOLD"), 5,
                          "NLOLD");
  output += integer_field(require<std::int64_t>(fields_, "NLNEW"), 5,
                          "NLNEW");
  output += control_suffix_;
  output += control_line_ending_;

  for (const auto& names : kInitialRecords) {
    append_named_record(output, fields_, names, record_line_ending_);
  }
  for (const auto* name : kChordArrays) {
    append_vector_records(output, require<DoubleVector>(fields_, name), name,
                          record_line_ending_);
  }
  for (std::size_t record = 0; record < kLaterRecords.size(); ++record) {
    if (record == 5) {
      const auto& rseps = require<DoubleVector>(fields_, "RSEPS");
      const auto& zseps = require<DoubleVector>(fields_, "ZSEPS");
      append_real_record(output,
                         {{rseps[0], "RSEPS"}, {zseps[0], "ZSEPS"},
                          {rseps[1], "RSEPS"}, {zseps[1], "ZSEPS"}},
                         record_line_ending_);
    } else {
      append_named_record(output, fields_, kLaterRecords[record],
                          record_line_ending_);
    }
  }

  output += ' ';
  for (const auto* name : {"NSILOP0", "MAGPRI0", "NFCOIL0", "NESUM0"}) {
    output += integer_field(require<std::int64_t>(fields_, name), 5, name);
  }
  output += record_line_ending_;

  const auto& csilop = require<DoubleVector>(fields_, "CSILOP");
  const auto& cmpr2 = require<DoubleVector>(fields_, "CMPR2");
  DoubleVector combined(csilop.size() + cmpr2.size());
  if (csilop.size() != 0) {
    combined.head(csilop.size()) = csilop;
  }
  if (cmpr2.size() != 0) {
    combined.tail(cmpr2.size()) = cmpr2;
  }
  append_vector_records(output, combined, "CSILOP/CMPR2",
                        record_line_ending_);
  append_vector_records(output, require<DoubleVector>(fields_, "CCBRSP"),
                        "CCBRSP", record_line_ending_);
  append_vector_records(output, require<DoubleVector>(fields_, "ECCURT"),
                        "ECCURT", record_line_ending_);

  for (const auto& names : kOptionalRecords) {
    if (!all_fields_present(fields_, names)) {
      break;
    }
    append_named_record(output, fields_, names, record_line_ending_);
  }
  output += footer_;
  detail::write_binary_file(path, output);
}

}  // namespace eqmdsk
