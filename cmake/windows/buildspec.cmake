# CMake Windows build dependencies module
#
# Adapted from the official OBS plugin template (obsproject/obs-plugintemplate, GPL-2.0-or-later).

include_guard(GLOBAL)

include(buildspec_common)

# Where downloaded dependencies live. The default sits inside the source tree like the OBS
# plugin template. Point it elsewhere when the source tree is in a synced folder such as
# OneDrive, because the dependencies are several gigabytes once extracted and built.
set(
  RELAYDOCK_DEPS_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/.deps"
  CACHE PATH
  "Directory for downloaded OBS build dependencies"
)

option(RELAYDOCK_SKIP_OBS_DEBUG_BUILD "Build only the Release configuration of libobs" OFF)

# _check_dependencies_windows: Set up Windows slice for _check_dependencies
function(_check_dependencies_windows)
  set(arch ${CMAKE_VS_PLATFORM_NAME})
  set(platform windows-${arch})

  file(TO_CMAKE_PATH "${RELAYDOCK_DEPS_DIR}" dependencies_dir)
  file(MAKE_DIRECTORY "${dependencies_dir}")

  set(prebuilt_filename "windows-deps-VERSION-ARCH-REVISION.zip")
  set(prebuilt_destination "obs-deps-VERSION-ARCH")
  set(qt6_filename "windows-deps-qt6-VERSION-ARCH-REVISION.zip")
  set(qt6_destination "obs-deps-qt6-VERSION-ARCH")
  set(obs-studio_filename "VERSION.zip")
  set(obs-studio_destination "obs-studio-VERSION")
  set(dependencies_list prebuilt qt6 obs-studio)

  _check_dependencies()
endfunction()

_check_dependencies_windows()
