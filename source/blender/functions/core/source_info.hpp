#pragma once

#include <string>

namespace FN {

class SourceInfo {
 public:
  virtual ~SourceInfo()
  {
  }

  virtual std::string to_string() const = 0;
  virtual void handle_warning(StringRef msg) const;
};

} /* namespace FN */