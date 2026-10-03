# RelayDock build identity module
#
# Build number, commit and build date all come from Git so that two builds of the
# same commit produce the same values. That keeps release builds reproducible.
#
#   PLUGIN_BUILD_NUMBER  Number of commits reachable from HEAD. 0 outside a Git checkout.
#   PLUGIN_COMMIT        Short commit hash. Empty outside a Git checkout.
#   PLUGIN_BUILD_DATE    Commit date in UTC (YYYY-MM-DD). SOURCE_DATE_EPOCH wins when set.
#   PLUGIN_BUILD_YEAR    Year part of PLUGIN_BUILD_DATE.
#   PLUGIN_DIRTY         TRUE when the work tree has uncommitted changes.

include_guard(GLOBAL)

find_package(Git QUIET)

set(PLUGIN_BUILD_NUMBER 0)
set(PLUGIN_COMMIT "")
set(PLUGIN_DIRTY FALSE)
set(_build_epoch "")

if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _count
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _result
  )
  if(_result EQUAL 0 AND _count MATCHES "^[0-9]+$")
    set(PLUGIN_BUILD_NUMBER ${_count})
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=9 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _result
  )
  if(_result EQUAL 0)
    set(PLUGIN_COMMIT "${_commit}")
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" log -1 --format=%ct
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _epoch
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _result
  )
  if(_result EQUAL 0 AND _epoch MATCHES "^[0-9]+$")
    set(_build_epoch "${_epoch}")
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT _status STREQUAL "")
    set(PLUGIN_DIRTY TRUE)
  endif()
endif()

if(DEFINED ENV{SOURCE_DATE_EPOCH} AND "$ENV{SOURCE_DATE_EPOCH}" MATCHES "^[0-9]+$")
  set(_build_epoch "$ENV{SOURCE_DATE_EPOCH}")
endif()

if(_build_epoch STREQUAL "")
  string(TIMESTAMP PLUGIN_BUILD_DATE "%Y-%m-%d" UTC)
else()
  # string(TIMESTAMP) honours SOURCE_DATE_EPOCH, so route the commit time through it.
  set(_saved_epoch "$ENV{SOURCE_DATE_EPOCH}")
  set(ENV{SOURCE_DATE_EPOCH} "${_build_epoch}")
  string(TIMESTAMP PLUGIN_BUILD_DATE "%Y-%m-%d" UTC)
  if(_saved_epoch STREQUAL "")
    unset(ENV{SOURCE_DATE_EPOCH})
  else()
    set(ENV{SOURCE_DATE_EPOCH} "${_saved_epoch}")
  endif()
endif()

string(SUBSTRING "${PLUGIN_BUILD_DATE}" 0 4 PLUGIN_BUILD_YEAR)

message(
  STATUS
  "RelayDock ${PLUGIN_VERSION_FULL} build ${PLUGIN_BUILD_NUMBER} (${PLUGIN_COMMIT}) dated ${PLUGIN_BUILD_DATE}"
)
