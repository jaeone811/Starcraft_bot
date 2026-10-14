#include "Logger.h"

#include "IniFile.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace bb
{
  LogLevel parseLogLevel(const std::string& s)
  {
    const std::string l = toLower(s);
    if (l == "debug") return LogLevel::Debug;
    if (l == "warn") return LogLevel::Warn;
    if (l == "error") return LogLevel::Error;
    return LogLevel::Info;
  }

  bool ensureDirectory(const std::string& directory)
  {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(directory), ec);
    return std::filesystem::is_directory(std::filesystem::path(directory), ec);
  }

  bool Logger::open(const std::string& directory, const std::string& fileName)
  {
    close();
    ensureDirectory(directory);
    path_ = (std::filesystem::path(directory) / fileName).string();
    out_.open(path_, std::ios::out | std::ios::trunc);
    return out_.is_open();
  }

  void Logger::close()
  {
    if (out_.is_open())
    {
      out_.flush();
      out_.close();
    }
  }

  void Logger::log(LogLevel level, const std::string& module, const std::string& message)
  {
    if (level == LogLevel::Warn) ++warnings_;
    if (level == LogLevel::Error) ++errors_;
    if (!enabled(level) || !out_.is_open())
      return;
    static const char* names[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };
    out_ << "[f=" << frame_ << " t=" << formatGameTime(frame_) << "] " << names[static_cast<int>(level)]
         << " [" << module << "] " << message << '\n';
    // Warnings and errors are flushed immediately so they survive a crash.
    if (level >= LogLevel::Warn)
      out_.flush();
  }

  void Logger::flush()
  {
    if (out_.is_open())
      out_.flush();
  }

  std::string formatGameTime(int frame)
  {
    const int totalSeconds = frame / 24;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", totalSeconds / 60, totalSeconds % 60);
    return buf;
  }

  std::string timestampForFileName()
  {
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
  }
}
