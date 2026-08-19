#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "eqmdsk/eqmdsk.hpp"

#if defined(EQMDSK_FUZZ_FORMAT_G)
using TargetFile = eqmdsk::GFile;
constexpr const char* kFormatName = "g";
#elif defined(EQMDSK_FUZZ_FORMAT_A)
using TargetFile = eqmdsk::AFile;
constexpr const char* kFormatName = "a";
#elif defined(EQMDSK_FUZZ_FORMAT_K)
using TargetFile = eqmdsk::KFile;
constexpr const char* kFormatName = "k";
#elif defined(EQMDSK_FUZZ_FORMAT_S)
using TargetFile = eqmdsk::SFile;
constexpr const char* kFormatName = "s";
#else
#error "An EQMDSK_FUZZ_FORMAT_* definition is required"
#endif

namespace {

long process_id() noexcept {
#ifdef _WIN32
  return static_cast<long>(_getpid());
#else
  return static_cast<long>(getpid());
#endif
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  static const auto input_path =
      std::filesystem::temp_directory_path() /
      (std::string("eqmdsk-libfuzzer-") + kFormatName + "-" +
       std::to_string(process_id()) + ".input");
  {
    std::ofstream output(input_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return 0;
    }
    if (size != 0) {
      output.write(reinterpret_cast<const char*>(data),
                   static_cast<std::streamsize>(size));
    }
  }

  try {
    const TargetFile parsed(input_path);
    static_cast<void>(parsed);
  } catch (const eqmdsk::Error&) {
    // Invalid inputs are expected. Sanitizers detect memory/undefined behavior.
  }
  return 0;
}
