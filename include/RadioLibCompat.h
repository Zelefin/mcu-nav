#pragma once

#ifdef __cplusplus
#include <string>

class __FlashStringHelper;

class String {
 public:
  String() = default;
  String(const char *value) : value_(value == nullptr ? "" : value) {}

  const char *c_str() const {
    return value_.c_str();
  }

  size_t length() const {
    return value_.length();
  }

  String &operator=(const char *value) {
    value_ = value == nullptr ? "" : value;
    return *this;
  }

 private:
  std::string value_;
};
#endif
