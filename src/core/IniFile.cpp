#include "IniFile.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace bb
{
  std::string trim(const std::string& s)
  {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
  }

  std::string toLower(std::string s)
  {
    for (auto& ch : s)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
  }

  namespace
  {
    bool isComment(const std::string& t) { return t.empty() || t[0] == ';' || t[0] == '#'; }

    // Returns true and fills `name` if the trimmed line is a [section] header.
    bool sectionName(const std::string& t, std::string& name)
    {
      if (t.size() < 2 || t.front() != '[')
        return false;
      size_t close = t.find(']');
      if (close == std::string::npos)
        return false;
      name = toLower(trim(t.substr(1, close - 1)));
      return true;
    }

    // Returns true and fills key/value for "key = value" lines. Inline "; comment"
    // after the value is stripped.
    bool keyValue(const std::string& t, std::string& key, std::string& value)
    {
      size_t eq = t.find('=');
      if (eq == std::string::npos)
        return false;
      key = toLower(trim(t.substr(0, eq)));
      value = t.substr(eq + 1);
      size_t semi = value.find(';');
      if (semi != std::string::npos)
        value = value.substr(0, semi);
      value = trim(value);
      return !key.empty();
    }
  }

  bool IniFile::load(const std::string& path)
  {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      return false;
    std::stringstream ss;
    ss << in.rdbuf();
    parse(ss.str());
    return true;
  }

  bool IniFile::save(const std::string& path) const
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
      return false;
    out << toString();
    return static_cast<bool>(out);
  }

  void IniFile::parse(const std::string& text)
  {
    lines_.clear();
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line))
    {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      lines_.push_back(line);
    }
    // Strip a UTF-8 BOM so the first key is found.
    if (!lines_.empty() && lines_[0].rfind("\xEF\xBB\xBF", 0) == 0)
      lines_[0] = lines_[0].substr(3);
  }

  std::string IniFile::toString() const
  {
    std::string out;
    for (const auto& l : lines_)
    {
      out += l;
      out += "\r\n"; // Windows tools (and BWAPI's GetPrivateProfileString) are fine with CRLF
    }
    return out;
  }

  std::optional<std::string> IniFile::get(const std::string& section, const std::string& key) const
  {
    const std::string wantSection = toLower(section);
    const std::string wantKey = toLower(key);
    std::string current;
    for (const auto& raw : lines_)
    {
      const std::string t = trim(raw);
      if (isComment(t))
        continue;
      std::string name;
      if (sectionName(t, name))
      {
        current = name;
        continue;
      }
      std::string k, v;
      if (current == wantSection && keyValue(t, k, v) && k == wantKey)
        return v;
    }
    return std::nullopt;
  }

  std::string IniFile::getString(const std::string& section, const std::string& key, const std::string& fallback) const
  {
    auto v = get(section, key);
    return v ? *v : fallback;
  }

  int IniFile::getInt(const std::string& section, const std::string& key, int fallback) const
  {
    auto v = get(section, key);
    if (!v || v->empty())
      return fallback;
    try
    {
      size_t used = 0;
      int result = std::stoi(*v, &used);
      return used == v->size() ? result : fallback;
    }
    catch (...)
    {
      return fallback;
    }
  }

  bool IniFile::getBool(const std::string& section, const std::string& key, bool fallback) const
  {
    auto v = get(section, key);
    if (!v)
      return fallback;
    const std::string s = toLower(*v);
    if (s == "1" || s == "true" || s == "on" || s == "yes")
      return true;
    if (s == "0" || s == "false" || s == "off" || s == "no")
      return false;
    return fallback;
  }

  void IniFile::set(const std::string& section, const std::string& key, const std::string& value)
  {
    const std::string wantSection = toLower(section);
    const std::string wantKey = toLower(key);
    std::string current;
    bool sectionSeen = wantSection.empty();
    size_t insertAt = wantSection.empty() ? 0 : lines_.size();
    // For the global section, insert before the first [section].
    if (wantSection.empty())
    {
      insertAt = lines_.size();
      for (size_t i = 0; i < lines_.size(); ++i)
      {
        std::string name;
        if (sectionName(trim(lines_[i]), name)) { insertAt = i; break; }
      }
    }
    for (size_t i = 0; i < lines_.size(); ++i)
    {
      const std::string t = trim(lines_[i]);
      std::string name;
      if (!isComment(t) && sectionName(t, name))
      {
        if (current == wantSection && sectionSeen && !wantSection.empty())
        {
          // Leaving the wanted section: insert after its last non-blank line.
          size_t pos = i;
          while (pos > 0 && trim(lines_[pos - 1]).empty()) --pos;
          insertAt = pos;
          break;
        }
        current = name;
        if (current == wantSection)
        {
          sectionSeen = true;
          insertAt = lines_.size();
        }
        continue;
      }
      std::string k, v;
      if (current == wantSection && !isComment(t) && keyValue(t, k, v) && k == wantKey)
      {
        lines_[i] = key + " = " + value;
        return;
      }
    }
    if (!sectionSeen)
    {
      if (!lines_.empty() && !trim(lines_.back()).empty())
        lines_.push_back("");
      lines_.push_back("[" + section + "]");
      insertAt = lines_.size();
    }
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insertAt), key + " = " + value);
  }
}
