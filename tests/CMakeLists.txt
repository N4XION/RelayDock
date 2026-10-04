# RelayDock automated tests.
#
# One executable holds every unit and security test. CTest runs it once per suite so a failure
# names the area that broke.

set(
  RELAYDOCK_TEST_SOURCES
  unit/test_main.cpp
  unit/test_helpers.h
  support/fake_platforms.cpp
  support/fake_platforms.h
  support/fake_server.cpp
  support/fake_server.h
  unit/test_build_info.cpp
  unit/test_chat.cpp
  unit/test_chat_readers.cpp
  unit/test_destination_state.cpp
  unit/test_diagnostics.cpp
  unit/test_encoding.cpp
  unit/test_network.cpp
  unit/test_optimizer.cpp
  unit/test_providers.cpp
  unit/test_services.cpp
  unit/test_settings.cpp
  unit/test_sleep_inhibitor.cpp
  unit/test_stream_url.cpp
  unit/test_uninstall.cpp
  unit/test_update_download.cpp
  unit/test_utils.cpp
  unit/test_vertical_layout.cpp
  security/test_credentials.cpp
  security/test_diagnostics_leaks.cpp
  security/test_redactor.cpp
  security/test_log_redaction.cpp
)

add_executable(relaydock-tests ${RELAYDOCK_TEST_SOURCES})
# powrprof: the sleep inhibitor test asks Windows what it is being kept awake for.
# bcrypt: the test web server hashes the WebSocket key.
target_link_libraries(relaydock-tests PRIVATE relaydock-core powrprof bcrypt)
target_include_directories(relaydock-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/unit")
# The test files are large. Without /bigobj the compiler runs out of object file sections.
target_compile_options(relaydock-tests PRIVATE /bigobj)
target_compile_definitions(relaydock-tests PRIVATE RD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(relaydock-tests PROPERTIES FOLDER "RelayDock")

add_test(NAME unit COMMAND relaydock-tests --test-suite-exclude=security*)
add_test(NAME security COMMAND relaydock-tests --test-suite=security*)

# Stand-ins for Twitch and YouTube on this PC, used by tests/integration/Test-Chat.ps1.
add_executable(
  rd-chat-fake
  tools/chat-fake/chat_fake.cpp
  support/fake_platforms.cpp
  support/fake_platforms.h
  support/fake_server.cpp
  support/fake_server.h
)
target_link_libraries(rd-chat-fake PRIVATE relaydock-core bcrypt)
target_include_directories(rd-chat-fake PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(rd-chat-fake PROPERTIES FOLDER "RelayDock")

# A stand-in for the release pages of the project on this PC, used by
# tests/integration/Test-Update.ps1.
add_executable(rd-release-fake tools/release-fake/release_fake.cpp support/fake_server.cpp support/fake_server.h)
target_link_libraries(rd-release-fake PRIVATE relaydock-core bcrypt)
target_include_directories(rd-release-fake PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(rd-release-fake PROPERTIES FOLDER "RelayDock")

# Local RTMP ingest used by the integration tests. See tests/integration/README.md.
add_executable(rd-rtmp-sink tools/rtmp-sink/rtmp_sink.cpp)
target_link_libraries(rd-rtmp-sink PRIVATE relaydock-options ws2_32)
set_target_properties(rd-rtmp-sink PROPERTIES FOLDER "RelayDock")
