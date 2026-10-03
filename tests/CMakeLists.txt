# RelayDock automated tests.
#
# One executable holds every unit and security test. CTest runs it once per suite so a failure
# names the area that broke.

set(
  RELAYDOCK_TEST_SOURCES
  unit/test_main.cpp
  unit/test_helpers.h
  unit/test_build_info.cpp
  unit/test_encoding.cpp
  unit/test_providers.cpp
  unit/test_settings.cpp
  unit/test_stream_url.cpp
  unit/test_utils.cpp
  security/test_credentials.cpp
  security/test_redactor.cpp
  security/test_log_redaction.cpp
)

add_executable(relaydock-tests ${RELAYDOCK_TEST_SOURCES})
target_link_libraries(relaydock-tests PRIVATE relaydock-core)
target_include_directories(relaydock-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/unit")
# The test files are large. Without /bigobj the compiler runs out of object file sections.
target_compile_options(relaydock-tests PRIVATE /bigobj)
target_compile_definitions(relaydock-tests PRIVATE RD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(relaydock-tests PROPERTIES FOLDER "RelayDock")

add_test(NAME unit COMMAND relaydock-tests --test-suite-exclude=security*)
add_test(NAME security COMMAND relaydock-tests --test-suite=security*)
