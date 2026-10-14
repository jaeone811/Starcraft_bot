// Loads the built AI module the way BWAPI does: open the library, look up the
// "gameInit" and "newAIModule" exports, create the module and destroy it.
// No game is involved; this proves the binary loads and exports the right symbols.

#include <BWAPI/AIModule.h>
#include <BWAPI/Game.h>

#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
using LibHandle = HMODULE;
static LibHandle openLib(const char* p) { return LoadLibraryA(p); }
static void* symbol(LibHandle h, const char* n) { return reinterpret_cast<void*>(GetProcAddress(h, n)); }
static void closeLib(LibHandle h) { FreeLibrary(h); }
static unsigned long lastError() { return GetLastError(); }
#else
#include <dlfcn.h>
using LibHandle = void*;
static LibHandle openLib(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void* symbol(LibHandle h, const char* n) { return dlsym(h, n); }
static void closeLib(LibHandle h) { dlclose(h); }
static unsigned long lastError() { const char* e = dlerror(); if (e) std::printf("dlerror: %s\n", e); return 0; }
#endif

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    std::printf("usage: %s <path to BeginnerBot module>\n", argv[0]);
    return 2;
  }
  LibHandle lib = openLib(argv[1]);
  if (!lib)
  {
    std::printf("FAIL: could not load %s (error %lu)\n", argv[1], lastError());
    return 1;
  }
  using GameInitFn = void (*)(BWAPI::Game*);
  using NewModuleFn = BWAPI::AIModule* (*)();
  auto gameInit = reinterpret_cast<GameInitFn>(symbol(lib, "gameInit"));
  auto newAIModule = reinterpret_cast<NewModuleFn>(symbol(lib, "newAIModule"));
  if (!gameInit || !newAIModule)
  {
    std::printf("FAIL: exports missing (gameInit=%p newAIModule=%p)\n", reinterpret_cast<void*>(gameInit),
                reinterpret_cast<void*>(newAIModule));
    closeLib(lib);
    return 1;
  }
  gameInit(nullptr);
  BWAPI::AIModule* module = newAIModule();
  if (!module)
  {
    std::printf("FAIL: newAIModule returned null\n");
    closeLib(lib);
    return 1;
  }
  delete module;
  closeLib(lib);
  std::printf("OK: module loaded, exports found, AIModule created and destroyed\n");
  return 0;
}
