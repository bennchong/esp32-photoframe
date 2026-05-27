#pragma once

#include <esp_timer.h>

#include <string>

class String {
  public:
    String() = default;
    String(const char *value) : value_(value ? value : "") {}
    String(const std::string &value) : value_(value) {}
    String(const String &other) = default;
    String(String &&other) noexcept = default;

    String &operator=(const String &other) = default;
    String &operator=(String &&other) noexcept = default;
    String &operator=(const char *value)
    {
        value_ = value ? value : "";
        return *this;
    }

    String(int value) : value_(std::to_string(value)) {}
    String(unsigned int value) : value_(std::to_string(value)) {}
    String(long value) : value_(std::to_string(value)) {}
    String(unsigned long value) : value_(std::to_string(value)) {}
    String(long long value) : value_(std::to_string(value)) {}
    String(unsigned long long value) : value_(std::to_string(value)) {}
    String(float value) : value_(std::to_string(value)) {}
    String(double value) : value_(std::to_string(value)) {}

    const char *c_str() const
    {
        return value_.c_str();
    }

    size_t length() const
    {
        return value_.length();
    }

    bool operator==(const String &other) const
    {
        return value_ == other.value_;
    }

    bool operator!=(const String &other) const
    {
        return value_ != other.value_;
    }

    String operator+(const String &other) const
    {
        return String(value_ + other.value_);
    }

    String &operator+=(const String &other)
    {
        value_ += other.value_;
        return *this;
    }

  private:
    std::string value_;
};

inline String operator+(const char *lhs, const String &rhs)
{
    return String(std::string(lhs ? lhs : "") + std::string(rhs.c_str()));
}

inline unsigned long millis()
{
    return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
}
