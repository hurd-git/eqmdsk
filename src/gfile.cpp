#include "eqmdsk/gfile.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "detail/fortran.hpp"
#include "eqmdsk/error.hpp"
#include "eqmdsk/namelist.hpp"

namespace eqmdsk {
namespace {

const std::vector<std::string> kRequiredFields{
    "CASE",    "RDIM",   "ZDIM",   "RCENTR", "RLEFT", "ZMID",
    "RMAXIS",  "ZMAXIS", "SIMAG",  "SIBRY",  "BCENTR", "CURRENT",
    "FPOL",    "PRES",   "FFPRIM", "PPRIME", "PSIRZ", "QPSI",
    "NBBBS",   "LIMITR", "RBBBS",  "ZBBBS",  "RLIM", "ZLIM",
    "NW",      "NH"};

const std::vector<std::string> kOptionalFields{
    "KVTOR",          "RVTOR",          "NMASS",
    "PRESSW",         "PWPRIM",         "DMION",
    "RHOVN",          "KEECUR",         "EPOTEN",
    "IPLCOUT",        "IPLCOUT_NW",     "IPLCOUT_NH",
    "IPLCOUT_ISHOT",  "IPLCOUT_ITIME",  "RGRID",
    "ZGRID",          "IPLCOUT_PREFIX", "PCURRT",
    "PCURRZ",         "CJOR",           "R1SURF",
    "R2SURF",         "VOLP",           "BPOLSS",
    "UNPARSED_EXTENSION"};

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

std::size_t find_namelist_start(const std::string& input,
                                std::size_t begin) {
  for (std::size_t position = begin; position < input.size(); ++position) {
    if (position != 0 && input[position - 1] != '\n' &&
        input[position - 1] != '\r') {
      continue;
    }
    auto probe = position;
    while (probe < input.size() &&
           (input[probe] == ' ' || input[probe] == '\t')) {
      ++probe;
    }
    if (probe < input.size() &&
        (input[probe] == '&' || input[probe] == '$')) {
      return probe;
    }
  }
  return std::string::npos;
}

bool has_token_before(const std::string& input, std::size_t position,
                      std::size_t end) {
  while (position < end &&
         (input[position] == ' ' || input[position] == '\t' ||
          input[position] == '\r' || input[position] == '\n' ||
          input[position] == ',')) {
    ++position;
  }
  return position < end && input[position] != '\0';
}

DoubleVector vector_from_values(const std::vector<double>& values) {
  return to_vector(values);
}

}  // namespace

GFile::GFile(std::string path) : GFile(std::move(path), true) {}

GFile::GFile(std::string path, bool read_file)
    : FieldFile(std::move(path)),
      aux_namelist_(std::make_unique<Namelist>(Namelist::create())) {
  if (read_file) {
    parse(detail::read_binary_file(path_));
    cocos_ = detect_cocos();
  }
}

GFile GFile::create(std::size_t nw, std::size_t nh) {
  if (nw == 0 || nh == 0 || nw > 9999 || nh > 9999) {
    throw ValidationError("NW and NH must be in the range 1..9999");
  }
  const auto grid_size = detail::checked_product(nw, nh, "PSIRZ");
  GFile result({}, false);
  result.fields_.insert("CASE", std::string{}, true, 0);
  result.fields_.insert("NW", static_cast<std::int64_t>(nw), true, 1);
  result.fields_.insert("NH", static_cast<std::int64_t>(nh), true, 2);
  static constexpr const char* scalar_names[]{
      "RDIM", "ZDIM", "RCENTR", "RLEFT", "ZMID", "RMAXIS",
      "ZMAXIS", "SIMAG", "SIBRY", "BCENTR", "CURRENT"};
  for (std::size_t index = 0; index < std::size(scalar_names); ++index) {
    result.fields_.insert(scalar_names[index], 0.0, true, 3 + index);
  }
  result.fields_.insert("FPOL", DoubleVector::Zero(nw).eval(), true, 14);
  result.fields_.insert("PRES", DoubleVector::Zero(nw).eval(), true, 15);
  result.fields_.insert("FFPRIM", DoubleVector::Zero(nw).eval(), true, 16);
  result.fields_.insert("PPRIME", DoubleVector::Zero(nw).eval(), true, 17);
  result.fields_.insert(
      "PSIRZ",
      DoubleMatrix::Zero(static_cast<Eigen::Index>(nh),
                         static_cast<Eigen::Index>(nw)).eval(),
      true, 18);
  result.fields_.insert("QPSI", DoubleVector::Zero(nw).eval(), true, 19);
  result.fields_.insert("NBBBS", std::int64_t{0}, true, 20);
  result.fields_.insert("LIMITR", std::int64_t{0}, true, 21);
  result.fields_.insert("RBBBS", DoubleVector{}, true, 22);
  result.fields_.insert("ZBBBS", DoubleVector{}, true, 23);
  result.fields_.insert("RLIM", DoubleVector{}, true, 24);
  result.fields_.insert("ZLIM", DoubleVector{}, true, 25);
  result.mark_all_fields_missing();
  result.clear_missing("NW");
  result.clear_missing("NH");
  static_cast<void>(grid_size);
  return result;
}

GFile::~GFile() = default;
GFile::GFile(const GFile& other)
    : FieldFile(other),
      idum_(other.idum_),
      cocos_(other.cocos_),
      aux_namelist_(other.aux_namelist_
                        ? std::make_unique<Namelist>(*other.aux_namelist_)
                        : nullptr) {
  fields_ = other.fields_;
}
GFile& GFile::operator=(const GFile& other) {
  if (this != &other) {
    filename_ = other.filename_;
    path_ = other.path_;
    abspath_ = other.abspath_;
    fields_ = other.fields_;
    missing_fields_ = other.missing_fields_;
    idum_ = other.idum_;
    cocos_ = other.cocos_;
    aux_namelist_ = other.aux_namelist_
                        ? std::make_unique<Namelist>(*other.aux_namelist_)
                        : nullptr;
  }
  return *this;
}
GFile::GFile(GFile&&) noexcept = default;
GFile& GFile::operator=(GFile&&) noexcept = default;

const std::vector<std::string>& GFile::required_fields() const noexcept {
  return kRequiredFields;
}

const std::vector<std::string>& GFile::optional_fields() const noexcept {
  return kOptionalFields;
}

FieldKind GFile::field_kind(const std::string& name) const noexcept {
  if (name == "CASE") {
    return FieldKind::String;
  }
  if (name == "NW" || name == "NH" || name == "NBBBS" ||
      name == "LIMITR" || name == "KVTOR" || name == "NMASS" ||
      name == "KEECUR" || name == "IPLCOUT" || name == "IPLCOUT_NW" ||
      name == "IPLCOUT_NH" || name == "IPLCOUT_ISHOT" ||
      name == "IPLCOUT_ITIME") {
    return FieldKind::Integer;
  }
  if (name == "PSIRZ" || name == "PCURRT" || name == "PCURRZ") {
    return FieldKind::RealMatrix;
  }
  if (name == "FPOL" || name == "PRES" || name == "FFPRIM" ||
      name == "PPRIME" || name == "QPSI" || name == "RBBBS" ||
      name == "ZBBBS" || name == "RLIM" || name == "ZLIM" ||
      name == "PRESSW" || name == "PWPRIM" || name == "DMION" ||
      name == "RHOVN" || name == "EPOTEN" || name == "RGRID" ||
      name == "ZGRID" || name == "IPLCOUT_PREFIX" || name == "CJOR" ||
      name == "R1SURF" || name == "R2SURF" || name == "VOLP" ||
      name == "BPOLSS" || name == "UNPARSED_EXTENSION") {
    return FieldKind::RealVector;
  }
  if (name == "RDIM" || name == "ZDIM" || name == "RCENTR" ||
      name == "RLEFT" || name == "ZMID" || name == "RMAXIS" ||
      name == "ZMAXIS" || name == "SIMAG" || name == "SIBRY" ||
      name == "BCENTR" || name == "CURRENT" || name == "RVTOR") {
    return FieldKind::Real;
  }
  return FieldKind::Any;
}

void GFile::assign(std::string name, FieldValue value) {
  if (name == "RGRID" || name == "ZGRID") {
    throw FieldError(name + " is derived from the G-file grid fields");
  }

  FieldFile::assign(name, std::move(value));

  if (name == "IPLCOUT") {
    const auto* mode = std::get_if<std::int64_t>(&fields_.at("IPLCOUT"));
    if (mode != nullptr && *mode == 1) {
      update_derived_grids();
    } else {
      fields_.erase("RGRID");
      fields_.erase("ZGRID");
      clear_missing("RGRID");
      clear_missing("ZGRID");
    }
    return;
  }

  if (name == "RDIM" || name == "RLEFT" || name == "ZDIM" ||
      name == "ZMID") {
    update_derived_grids();
  }
}

void GFile::update_derived_grids() {
  if (!fields_.contains("IPLCOUT") ||
      !std::holds_alternative<std::int64_t>(fields_.at("IPLCOUT")) ||
      std::get<std::int64_t>(fields_.at("IPLCOUT")) != 1) {
    return;
  }
  const auto scalar = [this](const char* name) -> std::optional<double> {
    if (!fields_.contains(name) ||
        !std::holds_alternative<double>(fields_.at(name))) {
      return std::nullopt;
    }
    return std::get<double>(fields_.at(name));
  };
  const auto rdim = scalar("RDIM");
  const auto rleft = scalar("RLEFT");
  const auto zdim = scalar("ZDIM");
  const auto zmid = scalar("ZMID");
  if (!rdim || !rleft || !zdim || !zmid) {
    return;
  }

  DoubleVector rgrid(2);
  rgrid[0] = *rleft;
  rgrid[1] = *rleft + *rdim;
  DoubleVector zgrid(2);
  zgrid[0] = *zmid - *zdim / 2.0;
  zgrid[1] = *zmid + *zdim / 2.0;
  if (fields_.contains("RGRID")) {
    fields_.set("RGRID", std::move(rgrid));
  } else {
    fields_.insert("RGRID", std::move(rgrid), false, 38);
  }
  if (fields_.contains("ZGRID")) {
    fields_.set("ZGRID", std::move(zgrid));
  } else {
    fields_.insert("ZGRID", std::move(zgrid), false, 39);
  }
  clear_missing("RGRID");
  clear_missing("ZGRID");
}

void GFile::parse(const std::string& bytes) {
  const auto diagnostic_filename = detail::path_for_diagnostic(path_);
  const auto header = find_header(bytes, diagnostic_filename);
  idum_ = header.idum;

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

  const auto extension_begin = cursor.position();
  const auto aux_begin = find_namelist_start(bytes, extension_begin);
  const auto extension_end = aux_begin == std::string::npos
                                 ? bytes.size()
                                 : aux_begin;

  if (aux_begin != std::string::npos) {
    aux_namelist_->parse(bytes.substr(aux_begin), path_);
  }

  if (has_token_before(bytes, extension_begin, extension_end)) {
    detail::NumericCursor extension(bytes, extension_begin, diagnostic_filename);
    const auto kvtor = extension.next_integer("KVTOR");
    const auto rvtor = extension.next_real("RVTOR");
    const auto nmass = extension.next_integer("NMASS");
    if (kvtor < 0 || nmass < 0) {
      throw ParseError("KVTOR and NMASS must be non-negative",
                       diagnostic_filename);
    }
    fields_.insert("KVTOR", kvtor, true, 26);
    fields_.insert("RVTOR", rvtor, true, 27);
    fields_.insert("NMASS", nmass, true, 28);
    if (kvtor > 0) {
      fields_.insert("PRESSW", vector_from_values(
                                  extension.real_array(nw, "PRESSW")),
                      false, 29);
      fields_.insert("PWPRIM", vector_from_values(
                                  extension.real_array(nw, "PWPRIM")),
                      false, 30);
    }
    if (nmass > 0) {
      fields_.insert("DMION", vector_from_values(
                                  extension.real_array(nw, "DMION")),
                      false, 31);
    }
    fields_.insert("RHOVN", vector_from_values(
                                extension.real_array(nw, "RHOVN")),
                    false, 32);
    const auto keecur = extension.next_integer("KEECUR");
    if (keecur < 0) {
      throw ParseError("KEECUR must be non-negative", diagnostic_filename);
    }
    fields_.insert("KEECUR", keecur, true, 30);
    if (keecur > 0) {
      fields_.insert("EPOTEN", vector_from_values(
                                  extension.real_array(nw, "EPOTEN")),
                      false, 33);
    }

    if (has_token_before(bytes, extension.position(), extension_end)) {
      const auto grid_size = detail::checked_product(nw, nh, "PCURRT");
      const auto probe = extension.position_after_line_ending();

      // IPLCOUT=1 starts with a fixed-width 4I5 record.  It is important to
      // read that record by columns: adjacent I5 values such as NH=129 and
      // ISHOT=67590 are legally written as ``  12967590``.
      std::array<std::int64_t, 4> iplcout_header{};
      bool has_iplcout_header = false;
      const auto line_end = bytes.find('\n', probe);
      if (line_end != std::string::npos && line_end <= extension_end &&
          line_end - probe >= 20) {
        has_iplcout_header = true;
        for (std::size_t index = 0; index < 4; ++index) {
          const auto value = parse_integer_text(
              std::string_view(bytes.data() + probe + index * 5, 5));
          if (!value) {
            has_iplcout_header = false;
            break;
          }
          iplcout_header[index] = *value;
        }
        if (has_iplcout_header &&
            (iplcout_header[0] != static_cast<std::int64_t>(nw) ||
             iplcout_header[1] != static_cast<std::int64_t>(nh))) {
          has_iplcout_header = false;
        }
      }

      std::vector<double> remaining;
      std::vector<double> header_values;
      if (has_iplcout_header) {
        for (const auto value : iplcout_header) {
          header_values.push_back(static_cast<double>(value));
        }
        const auto data_begin = line_end + 1;
        detail::NumericCursor trailing(bytes, data_begin,
                                       diagnostic_filename);
        while (has_token_before(bytes, trailing.position(), extension_end)) {
          remaining.push_back(trailing.next_real("IPLCOUT=1 extension"));
        }
      } else {
        detail::NumericCursor trailing(bytes, probe, diagnostic_filename);
        while (has_token_before(bytes, trailing.position(), extension_end)) {
          remaining.push_back(trailing.next_real("G-file extension"));
        }
      }

      if (has_iplcout_header) {
        if (remaining.size() < 4 + grid_size) {
          header_values.insert(header_values.end(), remaining.begin(),
                               remaining.end());
          fields_.insert("UNPARSED_EXTENSION",
                         vector_from_values(std::move(header_values)), false,
                         41);
          return;
        }
        fields_.insert("IPLCOUT", static_cast<std::int64_t>(1), true, 32);
        fields_.insert("IPLCOUT_NW", iplcout_header[0],
                        false, 34);
        fields_.insert("IPLCOUT_NH", iplcout_header[1],
                        false, 35);
        fields_.insert("IPLCOUT_ISHOT", iplcout_header[2], false, 36);
        fields_.insert("IPLCOUT_ITIME", iplcout_header[3], false, 37);
        fields_.insert("RGRID", vector_from_values(
                                    {remaining[0], remaining[1]}),
                        false, 38);
        fields_.insert("ZGRID", vector_from_values(
                                    {remaining[2], remaining[3]}),
                        false, 39);
        const auto prefix_count = remaining.size() - 4 - grid_size;
        fields_.insert("IPLCOUT_PREFIX",
                        vector_from_values(std::vector<double>(
                            remaining.begin() + 4,
                            remaining.begin() + 4 + prefix_count)),
                        false, 40);
        DoubleMatrix pcurrt(static_cast<Eigen::Index>(nh),
                            static_cast<Eigen::Index>(nw));
        const auto matrix_begin = remaining.end() -
                                   static_cast<std::ptrdiff_t>(grid_size);
        for (std::size_t row = 0; row < nh; ++row) {
          for (std::size_t column = 0; column < nw; ++column) {
            pcurrt(static_cast<Eigen::Index>(row),
                   static_cast<Eigen::Index>(column)) =
                matrix_begin[row * nw + column];
          }
        }
        fields_.insert("PCURRT", std::move(pcurrt), false, 41);
      } else if (remaining.size() == grid_size + 5 * nw) {
        fields_.insert("IPLCOUT", static_cast<std::int64_t>(2), true, 32);
        const auto matrix_end = remaining.begin() + grid_size;
        DoubleMatrix pcurrz(static_cast<Eigen::Index>(nh),
                             static_cast<Eigen::Index>(nw));
        for (std::size_t row = 0; row < nh; ++row) {
          for (std::size_t column = 0; column < nw; ++column) {
            pcurrz(static_cast<Eigen::Index>(row),
                   static_cast<Eigen::Index>(column)) =
                remaining[row * nw + column];
          }
        }
        fields_.insert("PCURRZ", std::move(pcurrz), false, 33);
        const char* names[] = {"CJOR", "R1SURF", "R2SURF", "VOLP",
                               "BPOLSS"};
        for (std::size_t array = 0; array < 5; ++array) {
          fields_.insert(names[array], vector_from_values(std::vector<double>(
                                      matrix_end + array * nw,
                                      matrix_end + (array + 1) * nw)),
                          false, 34 + array);
        }
      } else {
        fields_.insert("UNPARSED_EXTENSION",
                       vector_from_values(std::move(remaining)), false, 41);
      }
    }
  }
}

void GFile::validate_for_write() const {
  validate_required_fields();
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

  if (fields_.contains("KVTOR") || fields_.contains("RVTOR") ||
      fields_.contains("NMASS") || fields_.contains("RHOVN") ||
      fields_.contains("KEECUR")) {
    for (const auto* name : {"KVTOR", "RVTOR", "NMASS", "RHOVN",
                             "KEECUR"}) {
      if (!fields_.contains(name)) {
        throw ValidationError(std::string("G-file extension is missing ") +
                              name);
      }
    }
    const auto kvtor = require<std::int64_t>(fields_, "KVTOR");
    const auto nmass = require<std::int64_t>(fields_, "NMASS");
    const auto keecur = require<std::int64_t>(fields_, "KEECUR");
    if (kvtor < 0 || nmass < 0 || keecur < 0) {
      throw ValidationError("KVTOR, NMASS, and KEECUR must be non-negative");
    }
    if (!std::isfinite(require<double>(fields_, "RVTOR"))) {
      throw ValidationError("RVTOR must be finite");
    }
    const auto check_vector = [&](const char* name) {
      if (!fields_.contains(name)) {
        throw ValidationError(std::string("G-file extension is missing ") +
                              name);
      }
      const auto& value = require<DoubleVector>(fields_, name);
      if (static_cast<std::size_t>(value.size()) != nw) {
        throw ValidationError(std::string(name) + " length must equal NW");
      }
      for (Eigen::Index index = 0; index < value.size(); ++index) {
        if (!std::isfinite(value[index])) {
          throw ValidationError(std::string(name) + " must be finite");
        }
      }
    };
    if (kvtor > 0) {
      check_vector("PRESSW");
      check_vector("PWPRIM");
    }
    if (nmass > 0) {
      check_vector("DMION");
    }
    check_vector("RHOVN");
    if (keecur > 0) {
      check_vector("EPOTEN");
    }
  }

  if (fields_.contains("IPLCOUT")) {
    const auto mode = require<std::int64_t>(fields_, "IPLCOUT");
    if (mode != 1 && mode != 2) {
      throw ValidationError("IPLCOUT must be 1 or 2");
    }
    if (mode == 1) {
      for (const auto* name : {"RGRID", "ZGRID"}) {
        const auto& value = require<DoubleVector>(fields_, name);
        if (value.size() != 2) {
          throw ValidationError(std::string(name) + " must contain two values");
        }
      }
      static_cast<void>(
          require<DoubleVector>(fields_, "IPLCOUT_PREFIX"));
      const auto& pcurrt = require<DoubleMatrix>(fields_, "PCURRT");
      if (pcurrt.rows() != static_cast<Eigen::Index>(nh) ||
          pcurrt.cols() != static_cast<Eigen::Index>(nw)) {
        throw ValidationError("PCURRT shape must be (NH, NW)");
      }
      for (const auto* name : {"RGRID", "ZGRID", "IPLCOUT_PREFIX"}) {
        const auto& value = require<DoubleVector>(fields_, name);
        for (Eigen::Index index = 0; index < value.size(); ++index) {
          if (!std::isfinite(value[index])) {
            throw ValidationError(std::string(name) + " must be finite");
          }
        }
      }
      for (Eigen::Index row = 0; row < pcurrt.rows(); ++row) {
        for (Eigen::Index column = 0; column < pcurrt.cols(); ++column) {
          if (!std::isfinite(pcurrt(row, column))) {
            throw ValidationError("PCURRT must be finite");
          }
        }
      }
    } else {
      const auto& pcurrz = require<DoubleMatrix>(fields_, "PCURRZ");
      if (pcurrz.rows() != static_cast<Eigen::Index>(nh) ||
          pcurrz.cols() != static_cast<Eigen::Index>(nw)) {
        throw ValidationError("PCURRZ shape must be (NH, NW)");
      }
      for (const auto* name : {"CJOR", "R1SURF", "R2SURF", "VOLP",
                               "BPOLSS"}) {
        const auto& value = require<DoubleVector>(fields_, name);
        if (static_cast<std::size_t>(value.size()) != nw) {
          throw ValidationError(std::string(name) + " length must equal NW");
        }
      }
    }
  }
  if (fields_.contains("UNPARSED_EXTENSION")) {
    static_cast<void>(require<DoubleVector>(fields_, "UNPARSED_EXTENSION"));
  }
}

void GFile::save(const std::string& path) const {
  validate_for_write();
  const auto nw = static_cast<std::size_t>(require<std::int64_t>(fields_, "NW"));
  const auto nh = static_cast<std::size_t>(require<std::int64_t>(fields_, "NH"));
  const auto nbbbs =
      static_cast<std::size_t>(require<std::int64_t>(fields_, "NBBBS"));
  const auto limitr =
      static_cast<std::size_t>(require<std::int64_t>(fields_, "LIMITR"));

  std::string output;
  auto case_text = require<std::string>(fields_, "CASE");
  output += case_text;
  output.append(48 - case_text.size(), ' ');
  std::ostringstream integers;
  integers.imbue(std::locale::classic());
  integers << std::setw(4) << idum_ << std::setw(4) << nw << std::setw(4) << nh;
  output += integers.str();
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

  if (fields_.contains("KVTOR")) {
    const auto kvtor = require<std::int64_t>(fields_, "KVTOR");
    const auto nmass = require<std::int64_t>(fields_, "NMASS");
    const auto keecur = require<std::int64_t>(fields_, "KEECUR");
    std::ostringstream extension_header;
    extension_header.imbue(std::locale::classic());
    extension_header << std::setw(5) << kvtor;
    output += extension_header.str();
    output += detail::format_e16_9(require<double>(fields_, "RVTOR"));
    extension_header.str("");
    extension_header.clear();
    extension_header << std::setw(5) << nmass;
    output += extension_header.str();
    output += '\n';
    if (kvtor > 0) {
      writer.values(require<DoubleVector>(fields_, "PRESSW"));
      writer.finish_line();
      writer.values(require<DoubleVector>(fields_, "PWPRIM"));
      writer.finish_line();
    }
    if (nmass > 0) {
      writer.values(require<DoubleVector>(fields_, "DMION"));
      writer.finish_line();
    }
    writer.values(require<DoubleVector>(fields_, "RHOVN"));
    writer.finish_line();
    std::ostringstream keecur_line;
    keecur_line.imbue(std::locale::classic());
    keecur_line << std::setw(5) << keecur << '\n';
    output += keecur_line.str();
    if (keecur > 0) {
      writer.values(require<DoubleVector>(fields_, "EPOTEN"));
      writer.finish_line();
    }
  }

  if (fields_.contains("IPLCOUT")) {
    const auto mode = require<std::int64_t>(fields_, "IPLCOUT");
    if (mode == 1) {
      std::ostringstream header_line;
      header_line.imbue(std::locale::classic());
      header_line << std::setw(5) << require<std::int64_t>(fields_, "IPLCOUT_NW")
                  << std::setw(5) << require<std::int64_t>(fields_, "IPLCOUT_NH")
                  << std::setw(5) << require<std::int64_t>(fields_, "IPLCOUT_ISHOT")
                  << std::setw(5) << require<std::int64_t>(fields_, "IPLCOUT_ITIME")
                  << '\n';
      output += header_line.str();
      writer.values(require<DoubleVector>(fields_, "RGRID"));
      writer.values(require<DoubleVector>(fields_, "ZGRID"));
      writer.finish_line();
      writer.values(require<DoubleVector>(fields_, "IPLCOUT_PREFIX"));
      const auto& pcurrt = require<DoubleMatrix>(fields_, "PCURRT");
      for (Eigen::Index row = 0; row < pcurrt.rows(); ++row) {
        for (Eigen::Index column = 0; column < pcurrt.cols(); ++column) {
          writer.value(pcurrt(row, column));
        }
      }
      writer.finish_line();
    } else {
      const auto& pcurrz = require<DoubleMatrix>(fields_, "PCURRZ");
      for (Eigen::Index row = 0; row < pcurrz.rows(); ++row) {
        for (Eigen::Index column = 0; column < pcurrz.cols(); ++column) {
          writer.value(pcurrz(row, column));
        }
      }
      writer.finish_line();
      for (const auto* name : {"CJOR", "R1SURF", "R2SURF", "VOLP",
                               "BPOLSS"}) {
        writer.values(require<DoubleVector>(fields_, name));
        writer.finish_line();
      }
    }
  }
  if (fields_.contains("UNPARSED_EXTENSION")) {
    writer.values(require<DoubleVector>(fields_, "UNPARSED_EXTENSION"));
    writer.finish_line();
  }
  if (aux_namelist_) {
    std::ostringstream namelist_output;
    aux_namelist_->write_to(namelist_output);
    output += namelist_output.str();
  }
  detail::write_binary_file(path, output);
}

CocosResult GFile::detect_cocos() const {
  const int current_sign = strict_sign(require<double>(fields_, "CURRENT"));
  const int field_sign = strict_sign(require<double>(fields_, "BCENTR"));
  const int flux_sign = strict_sign(require<double>(fields_, "SIBRY") -
                                    require<double>(fields_, "SIMAG"));
  if (current_sign == 0 || field_sign == 0 || flux_sign == 0) {
    return CocosResult(
        {}, "COCOS detection requires finite, non-zero CURRENT, BCENTR, "
            "and SIBRY-SIMAG");
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
    return CocosResult({}, nonfinite ? "QPSI contains non-finite values"
                                     : "QPSI contains mixed signs");
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
  return CocosResult(
      std::move(candidates),
      q_sign == 0
          ? "QPSI has no non-zero sign; toroidal-angle direction and flux "
            "units are not encoded in a G-file"
          : "toroidal-angle direction and flux units are not encoded in a "
            "G-file");
}

void GFile::select_cocos(int source) {
  const auto& candidates = cocos_.candidates();
  if (std::find(candidates.begin(), candidates.end(), source) ==
      candidates.end()) {
    throw CocosError("selected source COCOS is not a detected candidate",
                     cocos_);
  }
  if (cocos_.selected_optional().has_value() &&
      *cocos_.selected_optional() == source) {
    return;
  }
  cocos_.set_selected(source);
}

GFile& GFile::to_cocos(int to_cocos, std::optional<int> from_cocos) {
  const auto& destination = convention(to_cocos);
  int source_number = 0;
  if (from_cocos.has_value()) {
    source_number = *from_cocos;
  } else if (cocos_.selected_optional().has_value()) {
    source_number = *cocos_.selected_optional();
  } else {
    throw CocosError("source COCOS is not selected", cocos_);
  }
  const auto& source = convention(source_number);
  if (source.number == destination.number) {
    auto detected = detect_cocos();
    detected.set_selected(to_cocos);
    cocos_ = std::move(detected);
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
  auto detected = detect_cocos();
  detected.set_selected(to_cocos);
  cocos_ = std::move(detected);
  return *this;
}

GFile GFile::converted_to_cocos(int to_cocos,
                                std::optional<int> from_cocos) const {
  GFile result(*this);
  result.to_cocos(to_cocos, from_cocos);
  return result;
}

}  // namespace eqmdsk
