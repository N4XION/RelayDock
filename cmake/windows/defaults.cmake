# CMake Windows defaults module
#
# Adapted from the official OBS plugin template (obsproject/obs-plugintemplate, GPL-2.0-or-later).

include_guard(GLOBAL)

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

include(buildspec)

# OBS loads third-party plugins from %ProgramData%\obs-studio\plugins\<name>.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(
    CMAKE_INSTALL_PREFIX
    "$ENV{ALLUSERSPROFILE}/obs-studio/plugins"
    CACHE STRING
    "Default plugin installation directory"
    FORCE
  )
endif()
