# CMake Windows helper functions module
#
# Adapted from the official OBS plugin template (obsproject/obs-plugintemplate, GPL-2.0-or-later).

include_guard(GLOBAL)

# set_target_properties_plugin: Install rules, run directory and version resource for the OBS module.
#
# Install layout (relative to the install prefix), as OBS expects it:
#   <target>/bin/64bit/<target>.dll
#   <target>/data/...
#
# Debug symbols install in their own "Symbols" component so user downloads stay small.
function(set_target_properties_plugin target)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs PROPERTIES)
  cmake_parse_arguments(PARSE_ARGV 0 _STPO "${options}" "${oneValueArgs}" "${multiValueArgs}")

  while(_STPO_PROPERTIES)
    list(POP_FRONT _STPO_PROPERTIES key value)
    set_property(TARGET ${target} PROPERTY ${key} "${value}")
  endwhile()

  install(
    TARGETS ${target}
    RUNTIME DESTINATION "${target}/bin/64bit" COMPONENT Runtime
    LIBRARY DESTINATION "${target}/bin/64bit" COMPONENT Runtime
  )

  install(
    FILES "$<TARGET_PDB_FILE:${target}>"
    CONFIGURATIONS RelWithDebInfo Debug Release
    DESTINATION "${target}/bin/64bit"
    COMPONENT Symbols
    EXCLUDE_FROM_ALL
    OPTIONAL
  )

  # rundir mirrors what OBS reads through OBS_PLUGINS_PATH and OBS_PLUGINS_DATA_PATH:
  #   rundir/<config>/bin/<target>.dll
  #   rundir/<config>/data/<target>/...
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/bin"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${target}>"
      "$<$<CONFIG:Debug,RelWithDebInfo,Release>:$<TARGET_PDB_FILE:${target}>>"
      "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/bin"
    COMMENT "Copy ${target} to rundir"
    VERBATIM
  )

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(
      DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
      DESTINATION "${target}/data"
      COMPONENT Runtime
      USE_SOURCE_PERMISSIONS
    )

    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/data/${target}"
      COMMAND
        "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/data/${target}"
      COMMENT "Copy ${target} data to rundir"
      VERBATIM
    )
  endif()

  set(CURRENT_YEAR "${PLUGIN_BUILD_YEAR}")
  configure_file(cmake/windows/resources/resource.rc.in "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
  target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
endfunction()
