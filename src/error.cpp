#include "eqmdsk/error.hpp"

#include <sstream>
#include <utility>

namespace eqmdsk {
namespace {

std::string format_parse_error(const std::string& message,
                               const std::string& filename,
                               std::size_t line,
                               std::size_t column) {
  std::ostringstream out;
  if (!filename.empty()) {
    out << filename;
    if (line != 0) {
      out << ':' << line;
      if (column != 0) {
        out << ':' << column;
      }
    }
    out << ": ";
  }
  out << message;
  return out.str();
}

}  // namespace

ParseError::ParseError(std::string message, std::string filename,
                       std::size_t line, std::size_t column)
    : Error(format_parse_error(message, filename, line, column)),
      filename_(std::move(filename)),
      line_(line),
      column_(column) {}

}  // namespace eqmdsk

