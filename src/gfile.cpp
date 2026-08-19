#include "eqmdsk/gfile.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "detail/fortran.hpp"
#include "eqmdsk/error.hpp"

namespace eqmdsk {
namespace {

struct Header {
  std::string case_text;
  std::string preamble;
  std::string suffix;
  int idum = 0;
  std::int64_t nw = 0;
  std::int64_t nh = 0;
  std::size_t body_offset = 0;
  std::size_t header_offset = 0;
};

std::optional<std::int64_t> parse_integer_text(std::string_view text) {
  const auto trimmed = detail::trim_copy(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  try {
    std::size_t consumed = 0;
    const auto value = std::stoll(trimmed, &consumed, 10);
    if (consumed != trimmed.size()) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Header> parse_header_line(std::string_view line,
                                        std::size_t header_offset,
                                        std::size_t body_offset,
                                        std::string preamble) {
  if (!line.empty() && line.back() == '\r') {
    line.remove_suffix(1);
  }
  if (line.size() >= 60) {
    const auto idum = parse_integer_text(line.substr(48, 4));
    const auto nw = parse_integer_text(line.substr(52, 4));
    const auto nh = parse_integer_text(line.substr(56, 4));
    if (idum && nw && nh && *nw > 0 && *nh > 0) {
      Header result;
      result.case_text = detail::rtrim_copy(line.substr(0, 48));
      result.preamble = std::move(preamble);
      result.suffix = std::string(line.substr(60));
      result.idum = static_cast<int>(*idum);
      result.nw = *nw;
      result.nh = *nh;
      result.body_offset = body_offset;
      result.header_offset = header_offset;
      return result;
    }
  }

  // Whitespace-separated fallback. The final three tokens are IDUM, NW, NH.
  std::vector<std::pair<std::string, std::size_t>> tokens;
  std::size_t position = 0;
  const auto is_header_space = [](char value) noexcept {
    return value == ' ' || value == '\t' || value == '\v' || value == '\f';
  };
  while (position < line.size()) {
    while (position < line.size() && is_header_space(line[position])) {
      ++position;
    }
    if (position == line.size()) {
      break;
    }
    const auto begin = position;
    while (position < line.size() && !is_header_space(line[position])) {
      ++position;
    }
    tokens.emplace_back(std::string(line.substr(begin, position - begin)), begin);
  }
  if (tokens.size() < 3) {
    return std::nullopt;
  }
  const auto idum = parse_integer_text(tokens[tokens.size() - 3].first);
  const auto nw = parse_integer_text(tokens[tokens.size() - 2].first);
  const auto nh = parse_integer_text(tokens[tokens.size() - 1].first);
  if (!idum || !nw || !nh || *nw <= 0 || *nh <= 0) {
    return std::nullopt;
  }
  if (*idum < std::numeric_limits<int>::min() ||
      *idum > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  Header result;
  result.case_text = detail::rtrim_copy(
      line.substr(0, tokens[tokens.size() - 3].second));
  result.preamble = std::move(preamble);
  result.idum = static_cast<int>(*idum);
  result.nw = *nw;
  result.nh = *nh;
  result.body_offset = body_offset;
  result.header_offset = header_offset;
  return result;
}

Header find_header(const std::string& input, const std::string& filename) {
  std::size_t offset = 0;
  std::string preamble;
  for (int line_number = 0; line_number < 64 && offset < input.size();
       ++line_number) {
    const auto newline = input.find('\n', offset);
    const auto end = newline == std::string::npos ? input.size() : newline;
    const auto body_offset = newline == std::string::npos ? end : newline + 1;
    const std::string_view line(input.data() + offset, end - offset);
    if (auto header = parse_header_line(line, offset, body_offset, preamble)) {
      return *header;
    }
    preamble.append(input, offset, body_offset - offset);
    offset = body_offset;
  }
  throw ParseError("unable to locate a valid GEQDSK header", filename, 1, 1);
}

template <typename T>
const T& require(const FieldMap& fields, const char* name) {
  const auto& value = fields.at(name);
  if (!std::holds_alternative<T>(value)) {
    throw ValidationError(std::string(name) + " has type " + field_type_name(value) +
                          ", expected a different type");
  }
  return std::get<T>(value);
}

template <typename T>
T& require(FieldMap& fields, const char* name) {
  auto& value = fields.at(name);
  if (!std::holds_alternative<T>(value)) {
    throw ValidationError(std::string(name) + " has type " + field_type_name(value) +
                          ", expected a different type");
  }
  return std::get<T>(value);
}

DoubleVector to_vector(std::vector<double> values) {
  DoubleVector result(static_cast<Eigen::Index>(values.size()));
  for (std::size_t i = 0; i < values.size(); ++i) {
    result[static_cast<Eigen::Index>(i)] = values[i];
  }
  return result;
}

int strict_sign(double value) noexcept {
  if (!std::isfinite(value) || value == 0.0) {
    return 0;
  }
  return value > 0.0 ? 1 : -1;
}

struct Convention {
  int number;
  int sigma_bp;
  int sigma_rphiz;
  int sigma_rhothetaphi;
  int exp_bp;
};

const std::array<Convention, 16>& conventions() {
  static const std::array<Convention, 16> values{{
      {1, 1, 1, 1, 0},   {2, 1, -1, 1, 0},
      {3, -1, 1, -1, 0}, {4, -1, -1, -1, 0},
      {5, 1, 1, -1, 0},  {6, 1, -1, -1, 0},
      {7, -1, 1, 1, 0},  {8, -1, -1, 1, 0},
      {11, 1, 1, 1, 1},  {12, 1, -1, 1, 1},
      {13, -1, 1, -1, 1}, {14, -1, -1, -1, 1},
      {15, 1, 1, -1, 1}, {16, 1, -1, -1, 1},
      {17, -1, 1, 1, 1}, {18, -1, -1, 1, 1},
  }};
  return values;
}

const Convention& convention(int number) {
  const auto& values = conventions();
  const auto found = std::find_if(values.begin(), values.end(),
                                  [number](const Convention& value) {
                                    return value.number == number;
                                  });
  if (found == values.end()) {
    throw CocosError("unsupported COCOS convention: " +
                     std::to_string(number));
  }
  return *found;
}

void multiply_scalar(FieldMap& fields, const char* name, double factor) {
  require<double>(fields, name) *= factor;
}

void multiply_vector(FieldMap& fields, const char* name, double factor) {
  require<DoubleVector>(fields, name) *= factor;
}

void multiply_matrix(FieldMap& fields, const char* name, double factor) {
  require<DoubleMatrix>(fields, name) *= factor;
}

}  // namespace

GFile::GFile(const std::filesystem::path& filename) : EFITFile(filename) {
  parse(detail::read_binary_file(filename));
  detect_cocos();
}

void GFile::parse(const std::string& bytes) {
  const auto diagnostic_filename = detail::path_for_diagnostic(filename_);
  const auto header = find_header(bytes, diagnostic_filename);
  idum_ = header.idum;
  preamble_ = header.preamble;
  header_suffix_ = header.suffix;
  extra_header_ = preamble_ + header_suffix_;

  std::size_t nw = 0;
  std::size_t nh = 0;
  try {
    nw = detail::checked_count(header.nw, "NW");
    nh = detail::checked_count(header.nh, "NH");
  } catch (const ValidationError& error) {
    throw ParseError(error.what(), diagnostic_filename, 1, 1);
  }
  if (nw == 0 || nh == 0) {
    throw ParseError("NW and NH must be positive", diagnostic_filename, 1, 1);
  }
  original_nw_ = nw;
  original_nh_ = nh;
  std::size_t grid_size = 0;
  try {
    grid_size = detail::checked_product(nw, nh, "PSIRZ");
  } catch (const ValidationError& error) {
    throw ParseError(error.what(), diagnostic_filename, 1, 1);
  }
  // A valid file needs at least 16 characters per value. This bounds hostile
  // dimensions before allocation without imposing an arbitrary grid limit.
  if (grid_size > bytes.size() || nw > bytes.size() || nh > bytes.size()) {
    throw ParseError("declared grid dimensions exceed the available file data",
                     diagnostic_filename, 1, 1);
  }

  detail::NumericCursor cursor(bytes, header.body_offset, diagnostic_filename);
  std::array<double, 20> scalars{};
  static constexpr std::array<const char*, 20> scalar_context{{
      "RDIM", "ZDIM", "RCENTR", "RLEFT", "ZMID", "RMAXIS", "ZMAXIS",
      "SIMAG", "SIBRY", "BCENTR", "CURRENT", "SIMAG(repeated)", "XDUM",
      "RMAXIS(repeated)", "XDUM", "ZMAXIS(repeated)", "XDUM",
      "SIBRY(repeated)", "XDUM", "XDUM"}};
  for (std::size_t i = 0; i < scalars.size(); ++i) {
    scalars[i] = cursor.next_real(scalar_context[i]);
  }

  fields_.insert("CASE", header.case_text, true, 0);
  fields_.insert("NW", static_cast<std::int64_t>(nw), true, 1);
  fields_.insert("NH", static_cast<std::int64_t>(nh), true, 2);
  static constexpr std::array<const char*, 11> scalar_names{{
      "RDIM", "ZDIM", "RCENTR", "RLEFT", "ZMID", "RMAXIS", "ZMAXIS",
      "SIMAG", "SIBRY", "BCENTR", "CURRENT"}};
  for (std::size_t i = 0; i < scalar_names.size(); ++i) {
    fields_.insert(scalar_names[i], scalars[i], true, 3 + i);
  }

  fields_.insert("FPOL", to_vector(cursor.real_array(nw, "FPOL")), true, 14);
  fields_.insert("PRES", to_vector(cursor.real_array(nw, "PRES")), true, 15);
  fields_.insert("FFPRIM", to_vector(cursor.real_array(nw, "FFPRIM")), true,
                 16);
  fields_.insert("PPRIME", to_vector(cursor.real_array(nw, "PPRIME")), true,
                 17);

  const auto flat = cursor.real_array(grid_size, "PSIRZ");
  DoubleMatrix psirz(static_cast<Eigen::Index>(nh),
                      static_cast<Eigen::Index>(nw));
  for (std::size_t row = 0; row < nh; ++row) {
    for (std::size_t column = 0; column < nw; ++column) {
      psirz(static_cast<Eigen::Index>(row),
            static_cast<Eigen::Index>(column)) = flat[row * nw + column];
    }
  }
  fields_.insert("PSIRZ", std::move(psirz), true, 18);
  fields_.insert("QPSI", to_vector(cursor.real_array(nw, "QPSI")), true, 19);

  std::int64_t nbbbs_raw = 0;
  std::int64_t limitr_raw = 0;
  if (cursor.has_nonspace()) {
    // Standard files have both counts. If the next content starts a namelist,
    // the optional boundary block is absent and the remainder is a raw tail.
    auto probe = cursor.position();
    while (probe < bytes.size() &&
           (bytes[probe] == ' ' || bytes[probe] == '\t' ||
            bytes[probe] == '\r' || bytes[probe] == '\n')) {
      ++probe;
    }
    if (probe < bytes.size() && bytes[probe] != '&' && bytes[probe] != '$' &&
        bytes[probe] != '\0') {
      nbbbs_raw = cursor.next_integer("NBBBS");
      limitr_raw = cursor.next_integer("LIMITR");
    }
  }
  std::size_t nbbbs = 0;
  std::size_t limitr = 0;
  try {
    nbbbs = detail::checked_count(nbbbs_raw, "NBBBS");
    limitr = detail::checked_count(limitr_raw, "LIMITR");
    static_cast<void>(detail::checked_product(nbbbs, 2, "boundary"));
    static_cast<void>(detail::checked_product(limitr, 2, "limiter"));
  } catch (const ValidationError& error) {
    throw ParseError(error.what(), diagnostic_filename);
  }
  if (nbbbs > bytes.size() || limitr > bytes.size()) {
    throw ParseError("declared boundary size exceeds available file data",
                     diagnostic_filename);
  }
  fields_.insert("NBBBS", nbbbs_raw, true, 20);
  fields_.insert("LIMITR", limitr_raw, true, 21);

  DoubleVector rbbbs(static_cast<Eigen::Index>(nbbbs));
  DoubleVector zbbbs(static_cast<Eigen::Index>(nbbbs));
  for (std::size_t i = 0; i < nbbbs; ++i) {
    rbbbs[static_cast<Eigen::Index>(i)] = cursor.next_real("RBBBS");
    zbbbs[static_cast<Eigen::Index>(i)] = cursor.next_real("ZBBBS");
  }
  DoubleVector rlim(static_cast<Eigen::Index>(limitr));
  DoubleVector zlim(static_cast<Eigen::Index>(limitr));
  for (std::size_t i = 0; i < limitr; ++i) {
    rlim[static_cast<Eigen::Index>(i)] = cursor.next_real("RLIM");
    zlim[static_cast<Eigen::Index>(i)] = cursor.next_real("ZLIM");
  }
  fields_.insert("RBBBS", std::move(rbbbs), true, 22);
  fields_.insert("ZBBBS", std::move(zbbbs), true, 23);
  fields_.insert("RLIM", std::move(rlim), true, 24);
  fields_.insert("ZLIM", std::move(zlim), true, 25);

  const auto tail_offset = cursor.position_after_line_ending();
  extension_tail_ = bytes.substr(tail_offset);
  if (!preamble_.empty()) {
    raw_sections_.push_back(
        RawSection{"preamble", preamble_, 0, false});
  }
  if (!header_suffix_.empty()) {
    raw_sections_.push_back(RawSection{"header_suffix", header_suffix_,
                                       header.header_offset + 60, false});
  }
  if (!extension_tail_.empty()) {
    raw_sections_.push_back(
        RawSection{"extension_tail", extension_tail_, tail_offset, false});
  }
}

void GFile::validate_for_write() const {
  const auto nw_value = require<std::int64_t>(fields_, "NW");
  const auto nh_value = require<std::int64_t>(fields_, "NH");
  const auto nbbbs_value = require<std::int64_t>(fields_, "NBBBS");
  const auto limitr_value = require<std::int64_t>(fields_, "LIMITR");
  const auto nw = detail::checked_count(nw_value, "NW");
  const auto nh = detail::checked_count(nh_value, "NH");
  const auto nbbbs = detail::checked_count(nbbbs_value, "NBBBS");
  const auto limitr = detail::checked_count(limitr_value, "LIMITR");
  if (nw == 0 || nh == 0 || nw > 9999 || nh > 9999) {
    throw ValidationError("NW and NH must be in the range 1..9999");
  }
  if (nbbbs > 99999 || limitr > 99999) {
    throw ValidationError("NBBBS and LIMITR must fit five-character fields");
  }
  if (idum_ < -999 || idum_ > 9999) {
    throw ValidationError("GEQDSK IDUM must fit a four-character field");
  }
  const auto& case_text = require<std::string>(fields_, "CASE");
  if (case_text.size() > 48) {
    throw ValidationError("CASE must contain at most 48 bytes");
  }
  if (!std::all_of(case_text.begin(), case_text.end(),
                   [](unsigned char character) {
                     return character >= 0x20 && character <= 0x7e;
                   })) {
    throw ValidationError("CASE must contain printable ASCII");
  }
  if (!extension_tail_.empty() &&
      (nw != original_nw_ || nh != original_nh_)) {
    throw ValidationError(
        "NW/NH cannot change while an opaque dimension-dependent extension "
        "tail is present");
  }
  for (const auto* name : {"FPOL", "PRES", "FFPRIM", "PPRIME", "QPSI"}) {
    if (static_cast<std::size_t>(require<DoubleVector>(fields_, name).size()) !=
        nw) {
      throw ValidationError(std::string(name) + " length must equal NW");
    }
  }
  const auto& psirz = require<DoubleMatrix>(fields_, "PSIRZ");
  if (static_cast<std::size_t>(psirz.rows()) != nh ||
      static_cast<std::size_t>(psirz.cols()) != nw) {
    throw ValidationError("PSIRZ shape must be (NH, NW)");
  }
  if (static_cast<std::size_t>(require<DoubleVector>(fields_, "RBBBS").size()) !=
          nbbbs ||
      static_cast<std::size_t>(require<DoubleVector>(fields_, "ZBBBS").size()) !=
          nbbbs) {
    throw ValidationError("RBBBS and ZBBBS lengths must equal NBBBS");
  }
  if (static_cast<std::size_t>(require<DoubleVector>(fields_, "RLIM").size()) !=
          limitr ||
      static_cast<std::size_t>(require<DoubleVector>(fields_, "ZLIM").size()) !=
          limitr) {
    throw ValidationError("RLIM and ZLIM lengths must equal LIMITR");
  }
  for (const auto* name : {"RDIM", "ZDIM", "RCENTR", "RLEFT", "ZMID",
                           "RMAXIS", "ZMAXIS", "SIMAG", "SIBRY", "BCENTR",
                           "CURRENT"}) {
    if (!std::isfinite(require<double>(fields_, name))) {
      throw ValidationError(std::string(name) + " must be finite");
    }
  }
}

void GFile::write(const std::filesystem::path& path) const {
  validate_for_write();
  const auto nw = static_cast<std::size_t>(require<std::int64_t>(fields_, "NW"));
  const auto nh = static_cast<std::size_t>(require<std::int64_t>(fields_, "NH"));
  const auto nbbbs =
      static_cast<std::size_t>(require<std::int64_t>(fields_, "NBBBS"));
  const auto limitr =
      static_cast<std::size_t>(require<std::int64_t>(fields_, "LIMITR"));

  std::string output = preamble_;
  auto case_text = require<std::string>(fields_, "CASE");
  output += case_text;
  output.append(48 - case_text.size(), ' ');
  std::ostringstream integers;
  integers.imbue(std::locale::classic());
  integers << std::setw(4) << idum_ << std::setw(4) << nw << std::setw(4) << nh;
  output += integers.str();
  output += header_suffix_;
  output += '\n';

  detail::FortranRealWriter writer(output);
  for (const auto* name : {"RDIM", "ZDIM", "RCENTR", "RLEFT", "ZMID",
                           "RMAXIS", "ZMAXIS", "SIMAG", "SIBRY", "BCENTR"}) {
    writer.value(require<double>(fields_, name));
  }
  writer.value(require<double>(fields_, "CURRENT"));
  writer.value(require<double>(fields_, "SIMAG"));
  writer.value(0.0);
  writer.value(require<double>(fields_, "RMAXIS"));
  writer.value(0.0);
  writer.value(require<double>(fields_, "ZMAXIS"));
  writer.value(0.0);
  writer.value(require<double>(fields_, "SIBRY"));
  writer.value(0.0);
  writer.value(0.0);
  writer.finish_line();

  for (const auto* name : {"FPOL", "PRES", "FFPRIM", "PPRIME"}) {
    writer.values(require<DoubleVector>(fields_, name));
    writer.finish_line();
  }
  const auto& psirz = require<DoubleMatrix>(fields_, "PSIRZ");
  for (std::size_t row = 0; row < nh; ++row) {
    for (std::size_t column = 0; column < nw; ++column) {
      writer.value(psirz(static_cast<Eigen::Index>(row),
                          static_cast<Eigen::Index>(column)));
    }
  }
  writer.finish_line();
  writer.values(require<DoubleVector>(fields_, "QPSI"));
  writer.finish_line();

  std::ostringstream counts;
  counts.imbue(std::locale::classic());
  counts << std::setw(5) << nbbbs << std::setw(5) << limitr << '\n';
  output += counts.str();
  const auto& rbbbs = require<DoubleVector>(fields_, "RBBBS");
  const auto& zbbbs = require<DoubleVector>(fields_, "ZBBBS");
  for (std::size_t i = 0; i < nbbbs; ++i) {
    writer.value(rbbbs[static_cast<Eigen::Index>(i)]);
    writer.value(zbbbs[static_cast<Eigen::Index>(i)]);
  }
  writer.finish_line();
  const auto& rlim = require<DoubleVector>(fields_, "RLIM");
  const auto& zlim = require<DoubleVector>(fields_, "ZLIM");
  for (std::size_t i = 0; i < limitr; ++i) {
    writer.value(rlim[static_cast<Eigen::Index>(i)]);
    writer.value(zlim[static_cast<Eigen::Index>(i)]);
  }
  writer.finish_line();
  output += extension_tail_;
  detail::write_binary_file(path, output);
}

void GFile::detect_cocos() {
  const int current_sign = strict_sign(require<double>(fields_, "CURRENT"));
  const int field_sign = strict_sign(require<double>(fields_, "BCENTR"));
  const int flux_sign = strict_sign(require<double>(fields_, "SIBRY") -
                                    require<double>(fields_, "SIMAG"));
  if (current_sign == 0 || field_sign == 0 || flux_sign == 0) {
    cocos_ = CocosResult({},
                         "COCOS detection requires finite, non-zero CURRENT, "
                         "BCENTR, and SIBRY-SIMAG");
    return;
  }

  const auto& qpsi = require<DoubleVector>(fields_, "QPSI");
  bool positive = false;
  bool negative = false;
  bool nonfinite = false;
  for (Eigen::Index i = 0; i < qpsi.size(); ++i) {
    if (!std::isfinite(qpsi[i])) {
      nonfinite = true;
    } else if (qpsi[i] > 0.0) {
      positive = true;
    } else if (qpsi[i] < 0.0) {
      negative = true;
    }
  }
  if (nonfinite || (positive && negative)) {
    cocos_ = CocosResult({}, nonfinite ? "QPSI contains non-finite values"
                                       : "QPSI contains mixed signs");
    return;
  }
  const int q_sign = positive ? 1 : (negative ? -1 : 0);
  const int sigma_bp = current_sign * flux_sign;
  const int sigma_rhothetaphi = current_sign * field_sign * q_sign;
  std::vector<int> candidates;
  for (const auto& item : conventions()) {
    if (item.sigma_bp == sigma_bp &&
        (q_sign == 0 || item.sigma_rhothetaphi == sigma_rhothetaphi)) {
      candidates.push_back(item.number);
    }
  }
  cocos_ = CocosResult(
      std::move(candidates),
      q_sign == 0
          ? "QPSI has no non-zero sign; toroidal-angle direction and flux "
            "units are not encoded in a G-file"
          : "toroidal-angle direction and flux units are not encoded in a "
            "G-file");
}

void GFile::select_cocos(int source) {
  static_cast<void>(convention(source));
  const auto& candidates = cocos_.candidates();
  if (std::find(candidates.begin(), candidates.end(), source) ==
      candidates.end()) {
    throw CocosError("selected source COCOS is not a detected candidate",
                     cocos_);
  }
  cocos_ = CocosResult(source, {source}, "source COCOS explicitly selected");
}

GFile& GFile::to_cocos(int target) {
  const auto& destination = convention(target);
  if (!cocos_.is_unique()) {
    throw CocosError("source COCOS is ambiguous or unknown", cocos_);
  }
  const auto& source = convention(cocos_.selected());
  if (source.number == destination.number) {
    return *this;
  }

  const double phi = static_cast<double>(destination.sigma_rphiz *
                                         source.sigma_rphiz);
  const double bp =
      static_cast<double>(destination.sigma_bp * source.sigma_bp);
  const double rho = static_cast<double>(destination.sigma_rhothetaphi *
                                         source.sigma_rhothetaphi);
  const double scale = std::pow(
      2.0 * std::acos(-1.0), destination.exp_bp - source.exp_bp);

  // Validate every transformed field before mutating any of them.
  static_cast<void>(require<double>(fields_, "CURRENT"));
  static_cast<void>(require<double>(fields_, "BCENTR"));
  static_cast<void>(require<DoubleVector>(fields_, "FPOL"));
  static_cast<void>(require<double>(fields_, "SIMAG"));
  static_cast<void>(require<double>(fields_, "SIBRY"));
  static_cast<void>(require<DoubleMatrix>(fields_, "PSIRZ"));
  static_cast<void>(require<DoubleVector>(fields_, "PPRIME"));
  static_cast<void>(require<DoubleVector>(fields_, "FFPRIM"));
  static_cast<void>(require<DoubleVector>(fields_, "QPSI"));

  multiply_scalar(fields_, "CURRENT", phi);
  multiply_scalar(fields_, "BCENTR", phi);
  multiply_vector(fields_, "FPOL", phi);
  multiply_scalar(fields_, "SIMAG", bp * phi * scale);
  multiply_scalar(fields_, "SIBRY", bp * phi * scale);
  multiply_matrix(fields_, "PSIRZ", bp * phi * scale);
  multiply_vector(fields_, "PPRIME", bp * phi / scale);
  multiply_vector(fields_, "FFPRIM", bp * phi / scale);
  multiply_vector(fields_, "QPSI", rho);
  cocos_ = CocosResult(target, {target},
                       extension_tail_.empty()
                           ? "converted"
                           : "converted; opaque extension tail was preserved "
                             "without COCOS transformation");
  return *this;
}

GFile GFile::converted_to_cocos(int target) const {
  GFile result(*this);
  result.to_cocos(target);
  return result;
}

}  // namespace eqmdsk
