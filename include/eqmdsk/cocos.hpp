#pragma once

#include <optional>
#include <string>
#include <vector>

#include "eqmdsk/error.hpp"

namespace eqmdsk {

class GFile;

class CocosResult {
 public:
  CocosResult() = default;
  CocosResult(std::vector<int> candidates, std::string diagnostic = {});
  CocosResult(int selected, std::vector<int> candidates,
              std::string diagnostic = {});

  bool is_unique() const noexcept;
  bool is_ambiguous() const noexcept;
  bool has_match() const noexcept { return !candidates_.empty(); }
  const std::vector<int>& candidates() const noexcept { return candidates_; }
  const std::string& diagnostic() const noexcept { return diagnostic_; }
  std::optional<int> selected_optional() const noexcept { return selected_; }
  int selected() const;

 private:
  friend class GFile;
  void set_selected(int value) noexcept { selected_ = value; }

  std::optional<int> selected_;
  std::vector<int> candidates_;
  std::string diagnostic_;
};

class CocosError : public Error {
 public:
  explicit CocosError(std::string message, CocosResult result = {});

  const CocosResult& result() const noexcept { return result_; }

 private:
  CocosResult result_;
};

}  // namespace eqmdsk
