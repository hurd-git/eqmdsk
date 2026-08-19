#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace eqmdsk {

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class IOError : public Error {
 public:
  using Error::Error;
};

class ParseError : public Error {
 public:
  ParseError(std::string message, std::string filename = {}, std::size_t line = 0,
             std::size_t column = 0);

  const std::string& filename() const noexcept { return filename_; }
  std::size_t line() const noexcept { return line_; }
  std::size_t column() const noexcept { return column_; }

 private:
  std::string filename_;
  std::size_t line_ = 0;
  std::size_t column_ = 0;
};

class ValidationError : public Error {
 public:
  using Error::Error;
};

class FieldError : public Error {
 public:
  using Error::Error;
};

}  // namespace eqmdsk
