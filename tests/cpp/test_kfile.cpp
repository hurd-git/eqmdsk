#include <cassert>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "eqmdsk/kfile.hpp"

namespace {

std::string read_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, const std::string& value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

}  // namespace

int main(int argc, char** argv) {
  const auto temporary = std::filesystem::temp_directory_path() /
                         "eqmdsk-kfile-test.nml";
  const auto roundtrip = std::filesystem::temp_directory_path() /
                         "eqmdsk-kfile-roundtrip.nml";
  std::string source =
      "outside before\n"
      "$input\n"
      "  MiXeD = 1\n"
      "  array = 2*1.5D+01, 3, .5\n"
      "  ints = 1 2 3\n"
      "  flag = .TrUe.\n"
      "  short_true = T\n"
      "  short_false = FALSE\n"
      "  text = 'don''t', \"two words\"\n"
      "  z = (1.0D0, -2)\n"
      "  indexed(1:3) = 1 2 3\n"
      "  strange = foo(bar=>baz) ! retained comment\n"
      "  nullable = 1,,3\n"
      "  typed = 1\n"
      "  typed = (2, 3)\n"
      "  comma_end = 7,\n"
      "  huge = 10000001*1\n"
      "  ; COMMENTED_OUT = 99\n"
      "  dup = 1\n"
      "  dup = 2\n"
      "$end\n"
      "between groups\n"
      "&second\n"
      " value = 4\n"
      "&END\n"
      "&second\n"
      " value = 5\n"
      "/\n";
  source.append("outside after\0binary\n", 21);
  write_bytes(temporary, source);

  eqmdsk::KFile file(temporary);
  assert(std::string(file.format_name()) == "KFile");
  assert(file.sections().size() == 3);
  assert(file.section_count("INPUT") == 1);
  assert(file.section_count("second") == 2);
  assert(file.section("input").opener() == '$');
  assert(file.section("input").terminator() == "$end");
  assert(file.section("INPUT").entries().size() == 17);
  assert(file.section("INPUT").count("dup") == 2);
  assert(file.entry("input", "mixed").original_name() == "MiXeD");
  assert(file.entry("input", "indexed").subscript() == "1:3");
  assert(file.entry("input", "strange").values().front().kind() ==
         eqmdsk::NamelistValueKind::raw);
  assert(file.entry("input", "nullable").values().size() == 3);
  assert(file.entry("input", "nullable").values()[1].kind() ==
         eqmdsk::NamelistValueKind::null);
  assert(file.entry("input", "z").values().front().as_complex() ==
         std::complex<double>(1.0, -2.0));
  assert(file.contains("mixed"));
  eqmdsk::EFITFile& base_file = file;
  assert(base_file.contains("mixed"));
  assert(std::get<std::int64_t>(base_file.at("mixed")) == 1);
  assert(!file.contains("nullable"));
  assert(!file.contains("typed"));
  assert(!file.contains("COMMENTED_OUT"));
  assert(!file.contains("huge"));
  assert(file.entry("input", "huge").values()[0].repeat() == 10000001);
  assert(std::get<std::int64_t>(file.at("MIXED")) == 1);
  assert(std::get<std::int64_t>(file.at("DuP")) == 2);
  const auto& array = std::get<eqmdsk::DoubleVector>(file.at("array"));
  assert(array.size() == 4);
  assert(array[0] == 15.0 && array[1] == 15.0 && array[2] == 3.0 &&
         array[3] == 0.5);
  assert(std::get<eqmdsk::IntVector>(file.at("ints")).size() == 3);
  assert(std::get<bool>(file.at("short_true")));
  assert(!std::get<bool>(file.at("short_false")));
  assert(std::get<std::int64_t>(file.at("comma_end")) == 7);

  // An untouched document is preserved exactly, including outside binary data.
  file.write(roundtrip);
  assert(read_bytes(roundtrip) == source);

  // Lookup is case-insensitive, and modifying the final duplicate controls the
  // effective dictionary value while retaining both ordered assignments.
  file.set("InPuT", "dUp", {eqmdsk::NamelistValue::integer(9)}, 1);
  std::get<std::int64_t>(file.at("mixed")) = 7;
  // The mapping is the final authority if it is changed after ordered set().
  std::get<std::int64_t>(file.at("dup")) = 2;
  file.set("second", "value", {eqmdsk::NamelistValue::integer(6)}, 0, 1);
  file.write(roundtrip);
  const auto changed_bytes = read_bytes(roundtrip);
  assert(changed_bytes.find("! retained comment") != std::string::npos);
  assert(changed_bytes.size() >= 21);
  assert(changed_bytes.compare(changed_bytes.size() - 21, 21,
                               std::string("outside after\0binary\n", 21)) == 0);
  eqmdsk::KFile reparsed(roundtrip);
  assert(std::get<std::int64_t>(reparsed.at("mixed")) == 7);
  assert(std::get<std::int64_t>(reparsed.at("dup")) == 2);
  assert(reparsed.entry("second", "value", 0, 1).values()[0].as_integer() ==
         6);
  assert(reparsed.section("input").count("dup") == 2);
  assert(reparsed.entry("input", "indexed").subscript() == "1:3");
  assert(reparsed.entry("input", "strange").values().front().as_raw() ==
         "foo(bar=>baz)");

  const std::string null_comment_source =
      "&IN\n VALUE=1\n SEMI=1; keep this comment\n/\n";
  write_bytes(temporary, null_comment_source);
  eqmdsk::KFile null_comment(temporary);
  assert(std::get<std::int64_t>(null_comment.at("semi")) == 1);
  std::get<std::int64_t>(null_comment.at("SEMI")) = 2;
  null_comment.set("IN", "VALUE",
                   {eqmdsk::NamelistValue::integer(7),
                    eqmdsk::NamelistValue::null()});
  null_comment.write(roundtrip);
  const auto null_comment_output = read_bytes(roundtrip);
  assert(null_comment_output.find("; keep this comment") != std::string::npos);
  assert(null_comment_output.find("1*") != std::string::npos);
  const eqmdsk::KFile null_comment_reparsed(roundtrip);
  const auto& null_values =
      null_comment_reparsed.entry("IN", "VALUE").values();
  assert(null_values.size() == 2);
  assert(null_values[0].as_integer() == 7);
  assert(null_values[1].kind() == eqmdsk::NamelistValueKind::null);
  assert(std::get<std::int64_t>(null_comment_reparsed.at("SEMI")) == 2);

  const std::string budget_source =
      "&IN\n"
      " STRING_LIMIT=10000000*'x'\n"
      " A=1 1\n"
      " ELEMENT_LIMIT=10000000*1\n"
      " DUP=10000000*'x'\n"
      " DUP=2\n"
      "/\n";
  write_bytes(temporary, budget_source);
  const eqmdsk::KFile budget(temporary);
  assert(!budget.contains("STRING_LIMIT"));
  assert(std::get<eqmdsk::IntVector>(budget.at("A")).size() == 2);
  assert(!budget.contains("ELEMENT_LIMIT"));
  assert(std::get<std::int64_t>(budget.at("DUP")) == 2);
  assert(budget.entry("IN", "STRING_LIMIT").values()[0].repeat() ==
         10000000);
  budget.write(roundtrip);
  assert(read_bytes(roundtrip) == budget_source);

  if (argc > 1 && std::filesystem::exists(argv[1])) {
    const eqmdsk::KFile real(argv[1]);
    assert(real.sections().size() == 1);
    assert(real.section("IN1").entries().size() == 42);
    assert(real.keys().size() == 42);
    assert(std::get<std::int64_t>(real.at("kffcur")) == 1);
    assert(std::get<std::int64_t>(real.at("LIMITR")) == 60);
    assert(std::get<eqmdsk::DoubleVector>(real.at("xlim")).size() == 60);
  }

  std::error_code ignored;
  std::filesystem::remove(temporary, ignored);
  std::filesystem::remove(roundtrip, ignored);
}
