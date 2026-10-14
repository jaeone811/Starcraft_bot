// Entry points BWAPI looks up when it loads an AI module DLL
// (same signatures as BWAPI 4.4.0's ExampleAIModule/Source/Dll.cpp).

#include "BeginnerBot.h"

#include <BWAPI.h>

#ifdef _WIN32
#define BEGINNERBOT_EXPORT extern "C" __declspec(dllexport)
#else
#define BEGINNERBOT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

BEGINNERBOT_EXPORT void gameInit(BWAPI::Game* game)
{
  BWAPI::BroodwarPtr = game;
}

BEGINNERBOT_EXPORT BWAPI::AIModule* newAIModule()
{
  return new bb::BeginnerBot();
}
