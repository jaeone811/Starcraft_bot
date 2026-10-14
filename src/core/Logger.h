#pragma once
// Plain file logger. Lines look like:
//   [f=1234 t=00:51] INFO  [Production] Training Marine at Barracks#57

#include <fstream>
#include <string>

namespace bb
{
  enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

  LogLevel parseLogLevel(const std::string& s);

  class Logger
  {
  public:
    // Creates the directory if needed. Returns false if the file could not be opened;
    // the logger then silently drops messages (the bot keeps running).
    bool open(const std::string& directory, const std::string& fileName);
    void close();
    bool isOpen() const { return out_.is_open(); }
    const std::string& path() const { return path_; }

    void setLevel(LogLevel level) { level_ = level; }
    bool enabled(LogLevel level) const { return level >= level_; }
    void setFrame(int frame) { frame_ = frame; }

    void log(LogLevel level, const std::string& module, const std::string& message);
    void debug(const std::string& module, const std::string& m) { log(LogLevel::Debug, module, m); }
    void info(const std::string& module, const std::string& m) { log(LogLevel::Info, module, m); }
    void warn(const std::string& module, const std::string& m) { log(LogLevel::Warn, module, m); }
    void error(const std::string& module, const std::string& m) { log(LogLevel::Error, module, m); }

    void flush();
    int warningCount() const { return warnings_; }
    int errorCount() const { return errors_; }

  private:
    std::ofstream out_;
    std::string path_;
    LogLevel level_ = LogLevel::Info;
    int frame_ = 0;
    int warnings_ = 0;
    int errors_ = 0;
  };

  // "mm:ss" game time for a frame count (Fastest speed, ~24 frames per game second).
  std::string formatGameTime(int frame);
  // Local wall-clock timestamp "YYYYMMDD_HHMMSS" for file names.
  std::string timestampForFileName();
  bool ensureDirectory(const std::string& directory);
}
