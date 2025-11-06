#include "hydra/time_utils.hpp"

#include <chrono>
#include <ctime>
#include <stdexcept>
#include <vector>

namespace hydra {

std::string format_now(const std::string& format) {
  auto now      = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::vector<char> buffer(128);
  while (true) {
    size_t written =
        std::strftime(buffer.data(), buffer.size(), format.c_str(), &tm);
    if (written > 0) {
      return std::string(buffer.data(), written);
    }
    if (buffer.size() >= 4096) {
      throw std::runtime_error("Failed to format timestamp");
    }
    buffer.resize(buffer.size() * 2);
  }
}

} // namespace hydra
