#include "PlatformPaths.h"

#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace bb
{
  fs::path moduleDirectory()
  {
#ifdef _WIN32
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&moduleDirectory), &module))
    {
      wchar_t buffer[MAX_PATH];
      const DWORD n = GetModuleFileNameW(module, buffer, MAX_PATH);
      if (n > 0 && n < MAX_PATH)
        return fs::path(buffer).parent_path();
    }
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&moduleDirectory), &info) && info.dli_fname)
      return fs::absolute(fs::path(info.dli_fname)).parent_path();
#endif
    return {};
  }

  fs::path starcraftRoot()
  {
    std::error_code ec;
    if (fs::is_directory("bwapi-data", ec))
      return fs::current_path(ec);
    const fs::path dll = moduleDirectory();
    if (!dll.empty() && dll.has_parent_path() && dll.parent_path().filename() == "bwapi-data")
      return dll.parent_path().parent_path();
    return fs::current_path(ec);
  }
}
