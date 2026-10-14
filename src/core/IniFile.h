#pragma once
// Minimal INI reader/editor. Keeps the original lines (comments, ordering) so that
// editing a user's file only touches the keys we set.

#include <optional>
#include <string>
#include <vector>

namespace bb
{
  class IniFile
  {
  public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    void parse(const std::string& text);
    std::string toString() const;

    // Section and key names are case-insensitive. Keys before any [section] use "".
    std::optional<std::string> get(const std::string& section, const std::string& key) const;
    std::string getString(const std::string& section, const std::string& key, const std::string& fallback) const;
    int getInt(const std::string& section, const std::string& key, int fallback) const;
    bool getBool(const std::string& section, const std::string& key, bool fallback) const;

    // Replaces the value of an existing key (keeping its line position) or appends
    // it at the end of the section (creating the section if needed).
    void set(const std::string& section, const std::string& key, const std::string& value);

  private:
    std::vector<std::string> lines_;
  };

  std::string trim(const std::string& s);
  std::string toLower(std::string s);
}
