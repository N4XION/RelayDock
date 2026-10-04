cmake_minimum_required(VERSION 3.28...3.31)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

if(NOT OS_WINDOWS)
  message(
    FATAL_ERROR
    "RelayDock supports Windows x64 only at this time. "
    "The credential store and build scripts for other systems are not written yet."
  )
endif()

option(RELAYDOCK_BUILD_PLUGIN "Build the OBS plugin module (downloads and builds OBS dependencies)" ON)
option(RELAYDOCK_BUILD_TESTS "Build the automated tests" ON)
option(RELAYDOCK_TEST_HOOKS "Compile the scenario runner used by integration tests and screenshot capture" OFF)

include(compilerconfig)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources.cmake")

# Options shared by every RelayDock target.
add_library(relaydock-options INTERFACE)
target_compile_options(relaydock-options INTERFACE /W4 /external:anglebrackets /external:W0 /wd4324)
target_compile_definitions(
  relaydock-options
  INTERFACE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    # Keeps std::mutex usable when the installed Visual C++ runtime is older than the compiler.
    _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR
)

# ---------------------------------------------------------------------------------------------
# Core library: no OBS and no Qt. Everything here is covered by unit tests.
# ---------------------------------------------------------------------------------------------
add_library(relaydock-core STATIC ${RELAYDOCK_CORE_SOURCES})
target_include_directories(
  relaydock-core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_include_directories(relaydock-core SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(relaydock-core PUBLIC relaydock-options)
target_link_libraries(relaydock-core PUBLIC advapi32 crypt32 ws2_32 pdh iphlpapi winhttp)
set_target_properties(relaydock-core PROPERTIES FOLDER "RelayDock")

if(PLUGIN_DIRTY)
  set(_dirty 1)
else()
  set(_dirty 0)
endif()
configure_file(src/build_info.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info_generated.h" @ONLY)

# ---------------------------------------------------------------------------------------------
# OBS plugin module.
# ---------------------------------------------------------------------------------------------
if(RELAYDOCK_BUILD_PLUGIN)
  include(defaults)
  include(helpers)

  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

  add_library(${CMAKE_PROJECT_NAME} MODULE ${RELAYDOCK_PLUGIN_SOURCES})
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE relaydock-core OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC OFF FOLDER "RelayDock"
  )
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QT_NO_CAST_TO_ASCII QT_NO_KEYWORDS)
  # The linker writes the full path of the debug file into the DLL. That path names the folder
  # of whoever built it. Store the file name only, so a release carries no personal path and
  # two builds of the same commit produce the same file.
  target_link_options(${CMAKE_PROJECT_NAME} PRIVATE /PDBALTPATH:${_name}.pdb)

  if(RELAYDOCK_TEST_HOOKS)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${RELAYDOCK_TEST_HOOK_SOURCES})
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE RELAYDOCK_TEST_HOOKS=1)
  endif()

  set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
endif()

# ---------------------------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------------------------
if(RELAYDOCK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
