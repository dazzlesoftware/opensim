// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace opensim::framework {
class UnicodeRegex {
  public:
    explicit UnicodeRegex(const std::string& pattern);
    ~UnicodeRegex();
    std::optional<std::vector<std::string>> match(const std::string& text);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
std::string stream_reader_text(const std::string& bytes);
}
