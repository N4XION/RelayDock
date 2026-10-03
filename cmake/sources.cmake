# Source lists for RelayDock targets.
#
# RELAYDOCK_CORE_SOURCES       Logic with no OBS and no Qt dependency. Unit tested.
# RELAYDOCK_PLUGIN_SOURCES     OBS and Qt code for the plugin module.
# RELAYDOCK_TEST_HOOK_SOURCES  Scenario runner, compiled only with RELAYDOCK_TEST_HOOKS=ON.

set(
  RELAYDOCK_CORE_SOURCES
  src/build_info.cpp
  src/build_info.h
  src/core/types.cpp
  src/core/types.h
  src/core/user_message.h
  src/security/credential_store.cpp
  src/security/credential_store.h
  src/security/memory_credential_store.cpp
  src/security/memory_credential_store.h
  src/security/redactor.cpp
  src/security/redactor.h
  src/security/secret_string.cpp
  src/security/secret_string.h
  src/security/secret_vault.cpp
  src/security/secret_vault.h
  src/security/secure_zero.cpp
  src/security/secure_zero.h
  src/security/windows_credential_store.cpp
  src/security/windows_credential_store.h
  src/utils/clock.cpp
  src/utils/clock.h
  src/utils/log.cpp
  src/utils/log.h
  src/utils/strings.cpp
  src/utils/strings.h
  src/utils/uuid.cpp
  src/utils/uuid.h
)

set(RELAYDOCK_PLUGIN_SOURCES src/plugin-main.cpp)

set(RELAYDOCK_TEST_HOOK_SOURCES "")
