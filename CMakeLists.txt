cmake_minimum_required(VERSION 3.16)
project(BeginnerBot VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BEGINNERBOT_BUILD_TESTS "Build the unit tests" ON)
option(BEGINNERBOT_BUILD_CONFIGURATOR "Build the BeginnerBotConfig tool" ON)
set(BWAPI_DIR "" CACHE PATH "BWAPI 4.4.0 folder containing include/BWAPI.h and BWAPILIB/ (installer folder or <git checkout>/bwapi)")

if(MSVC)
  # BWAPI.dll is a 32-bit Release build linked against the shared MSVC runtime (/MD).
  # The AI module must match: Win32, /MD, Release.
  add_compile_options(/utf-8 /EHsc /W3)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  if(BWAPI_DIR AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "StarCraft 1.16.1 and BWAPI are 32-bit: configure with -A Win32")
  endif()
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------- core (no BWAPI)
add_library(bb_core STATIC
  src/core/BotSettings.cpp
  src/core/CommandHistory.cpp
  src/core/DifficultyConfig.cpp
  src/core/IniFile.cpp
  src/core/Logger.cpp
  src/core/Metrics.cpp
  src/core/Planning.cpp)
target_include_directories(bb_core PUBLIC src/core)
set_target_properties(bb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- the AI module
if(BWAPI_DIR)
  include(cmake/BWAPI.cmake)

  set(BOT_SOURCES
    src/bot/BeginnerBot.cpp
    src/bot/BuildingManager.cpp
    src/bot/BwapiUtil.cpp
    src/bot/CombatManager.cpp
    src/bot/Commander.cpp
    src/bot/InformationManager.cpp
    src/bot/MapInfo.cpp
    src/bot/PlatformPaths.cpp
    src/bot/ProductionManager.cpp
    src/bot/ScoutManager.cpp
    src/bot/StrategyManager.cpp
    src/bot/SupplyManager.cpp
    src/bot/WorkerManager.cpp)

  # Everything except the DLL entry points, so tests can link the bot code too.
  add_library(bb_bot STATIC ${BOT_SOURCES})
  target_include_directories(bb_bot PUBLIC src/bot)
  target_link_libraries(bb_bot PUBLIC bb_core bwapilib ${CMAKE_DL_LIBS})
  # The bot reports this version in game and in its log (single source: project() above).
  target_compile_definitions(bb_bot PRIVATE BEGINNERBOT_VERSION="${PROJECT_VERSION}")
  set_target_properties(bb_bot PROPERTIES POSITION_INDEPENDENT_CODE ON)

  add_library(BeginnerBot SHARED src/bot/Dll.cpp)
  target_link_libraries(BeginnerBot PRIVATE bb_bot)
  set_target_properties(BeginnerBot PROPERTIES PREFIX "" OUTPUT_NAME "BeginnerBot")
  if(NOT WIN32)
    message(WARNING "Building on a non-Windows host: the result is only a compile/link check. "
                    "The real bot is a 32-bit Windows DLL built with MSVC.")
  endif()
else()
  message(WARNING "BWAPI_DIR not set: only the BWAPI-independent core, tests and configurator are built.")
endif()

# ---------------------------------------------------------------- configurator
if(BEGINNERBOT_BUILD_CONFIGURATOR)
  add_executable(BeginnerBotConfig tools/configurator/main.cpp)
  target_link_libraries(BeginnerBotConfig PRIVATE bb_core)
endif()

# ---------------------------------------------------------------- tests
if(BEGINNERBOT_BUILD_TESTS)
  enable_testing()
  add_executable(bb_core_tests tests/core_tests.cpp)
  target_link_libraries(bb_core_tests PRIVATE bb_core)
  add_test(NAME core_tests COMMAND bb_core_tests)

  if(BWAPI_DIR)
    add_executable(bb_bwapi_tests tests/bwapi_tests.cpp)
    target_link_libraries(bb_bwapi_tests PRIVATE bb_bot)
    add_test(NAME bwapi_tests COMMAND bb_bwapi_tests)

    # Loads the built module like BWAPI does (LoadLibrary/dlopen + newAIModule).
    add_executable(bb_module_load_test tests/module_load_test.cpp)
    target_include_directories(bb_module_load_test SYSTEM PRIVATE "${BWAPI_DIR}/include")
    target_compile_definitions(bb_module_load_test PRIVATE NOMINMAX)
    if(NOT WIN32)
      target_link_libraries(bb_module_load_test PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_dependencies(bb_module_load_test BeginnerBot)
    add_test(NAME module_load_test COMMAND bb_module_load_test $<TARGET_FILE:BeginnerBot>)
  endif()
endif()

# ---------------------------------------------------------------- package layout
install(FILES config/BeginnerBot.ini DESTINATION bwapi-data/AI)
if(TARGET BeginnerBot)
  install(TARGETS BeginnerBot RUNTIME DESTINATION bwapi-data/AI LIBRARY DESTINATION bwapi-data/AI)
endif()
if(TARGET BeginnerBotConfig)
  install(TARGETS BeginnerBotConfig RUNTIME DESTINATION tools)
endif()
