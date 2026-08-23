#include "eqmdsk/cocos.hpp"

#include <algorithm>
#include <utility>

namespace eqmdsk {

CocosResult::CocosResult(std::vector<int> candidates, std::string diagnostic)
    : candidates_(std::move(candidates)), diagnostic_(std::move(diagnostic)) {
  std::sort(candidates_.begin(), candidates_.end());
  candidates_.erase(std::unique(candidates_.begin(), candidates_.end()),
                    candidates_.end());
  if (candidates_.size() == 1) {
    selected_ = candidates_.front();
  }
}

CocosResult::CocosResult(int selected, std::vector<int> candidates,
                         std::string diagnostic)
    : selected_(selected),
      candidates_(std::move(candidates)),
      diagnostic_(std::move(diagnostic)) {
  std::sort(candidates_.begin(), candidates_.end());
  candidates_.erase(std::unique(candidates_.begin(), candidates_.end()),
                    candidates_.end());
  if (std::find(candidates_.begin(), candidates_.end(), selected) ==
      candidates_.end()) {
    throw CocosError("selected COCOS is not present in the candidate set");
  }
}

CocosError::CocosError(std::string message, CocosResult result)
    : Error(std::move(message)), result_(std::move(result)) {}

bool CocosResult::is_unique() const noexcept {
  return candidates_.size() == 1;
}

bool CocosResult::is_ambiguous() const noexcept {
  return candidates_.size() > 1;
}

int CocosResult::selected() const {
  if (!selected_.has_value()) {
    throw CocosError("COCOS source is not selected");
  }
  return *selected_;
}

}  // namespace eqmdsk
