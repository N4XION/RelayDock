# RelayDock bootstrap module
#
# Adapted from the official OBS plugin template (obsproject/obs-plugintemplate, GPL-2.0-or-later).

include_guard(GLOBAL)

# Map fallback configurations for optimized build configurations
# gersemi: off
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO
    RelWithDebInfo
    Release
    MinSizeRel
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL
    MinSizeRel
    Release
    RelWithDebInfo
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELEASE
    Release
    RelWithDebInfo
    MinSizeRel
    None
    ""
)
# gersemi: on

# Prohibit in-source builds
if("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  message(
    FATAL_ERROR
    "In-source builds are not supported. "
    "Specify a build directory via 'cmake -S <SOURCE DIRECTORY> -B <BUILD_DIRECTORY>' instead."
  )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)
# A change of buildspec.json configures again at the next build.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json")

string(JSON _name GET ${buildspec} name)
string(JSON _display_name GET ${buildspec} displayName)
string(JSON _author GET ${buildspec} author)
string(JSON _version GET ${buildspec} version)
string(JSON _version_suffix GET ${buildspec} versionSuffix)
string(JSON _repository GET ${buildspec} repository)
string(JSON _twitch_client_id ERROR_VARIABLE _twitch_client_id_error GET ${buildspec} twitchClientId)
if(_twitch_client_id_error)
  set(_twitch_client_id "")
endif()
string(JSON _obs_minimum GET ${buildspec} obs minimumVersion)
string(JSON _obs_tested GET ${buildspec} obs testedVersions)

set(PLUGIN_AUTHOR "${_author}")
set(PLUGIN_DISPLAY_NAME "${_display_name}")
set(PLUGIN_VERSION "${_version}")
set(PLUGIN_VERSION_SUFFIX "${_version_suffix}")
set(PLUGIN_OBS_MINIMUM_VERSION "${_obs_minimum}")
set(PLUGIN_OBS_TESTED_VERSIONS "${_obs_tested}")

# GitHub repository in "owner/name" form, for links and the update check. buildspec.json is the
# only place that sets it. A fork that publishes its own releases changes it there.
#
# It is deliberately not a cache entry. An earlier version of this file cached it, and an
# existing build folder then kept the first value it had seen: a build without a project page
# after buildspec.json had gained one. The unset removes that old entry.
unset(RELAYDOCK_REPOSITORY CACHE)
set(RELAYDOCK_REPOSITORY "${_repository}")

# The id of the Twitch application that RelayDock signs users in with. It is a public name, not
# a secret: Twitch gives a public client no secret at all. A build without one still reads
# Twitch chat when the user enters an application id of their own under Settings, Chat.
set(RELAYDOCK_TWITCH_CLIENT_ID "${_twitch_client_id}")

if(PLUGIN_VERSION_SUFFIX STREQUAL "")
  set(PLUGIN_VERSION_FULL "${PLUGIN_VERSION}")
else()
  set(PLUGIN_VERSION_FULL "${PLUGIN_VERSION}-${PLUGIN_VERSION_SUFFIX}")
endif()

string(REPLACE "." ";" _version_canonical "${_version}")
list(GET _version_canonical 0 PLUGIN_VERSION_MAJOR)
list(GET _version_canonical 1 PLUGIN_VERSION_MINOR)
list(GET _version_canonical 2 PLUGIN_VERSION_PATCH)
unset(_version_canonical)

include(buildnumber)
include(osconfig)

# Disable exports automatically going into the CMake package registry
set(CMAKE_EXPORT_PACKAGE_REGISTRY FALSE)
# Enable default inclusion of targets' source and binary directory
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
