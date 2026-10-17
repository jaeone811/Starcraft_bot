// Entry points BWAPI looks up when it loads an AI module DLL
// (same signatures as BWAPI 4.4.0's ExampleAIModule/Source/Dll.cpp).

#include "JaeTankBot.h"

#include <BWAPI.h>

#ifdef _WIN32
#define JAETANKBOT_EXPORT extern "C" __declspec(dllexport)
#else
#define JAETANKBOT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

JAETANKBOT_EXPORT void gameInit(BWAPI::Game* game)
{
  BWAPI::BroodwarPtr = game;
}

JAETANKBOT_EXPORT BWAPI::AIModule* newAIModule()
{
  return new bb::JaeTankBot();
}
