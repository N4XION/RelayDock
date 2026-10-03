# Source lists for RelayDock targets.
#
# RELAYDOCK_CORE_SOURCES       Logic with no OBS and no Qt dependency. Unit tested.
# RELAYDOCK_PLUGIN_SOURCES     OBS and Qt code for the plugin module.
# RELAYDOCK_TEST_HOOK_SOURCES  Scenario runner, compiled only with RELAYDOCK_TEST_HOOKS=ON.

set(
  RELAYDOCK_CORE_SOURCES
  src/build_info.cpp
  src/build_info.h
  src/core/output_stop.h
  src/core/types.cpp
  src/core/types.h
  src/core/user_message.h
  src/encoders/encode_plan.cpp
  src/encoders/encode_plan.h
  src/encoders/encoder_caps.cpp
  src/encoders/encoder_caps.h
  src/encoders/video_math.cpp
  src/encoders/video_math.h
  src/network/stream_url.cpp
  src/network/stream_url.h
  src/performance/effective_settings.cpp
  src/performance/effective_settings.h
  src/providers/custom_rtmp/custom_rtmp_provider.cpp
  src/providers/custom_rtmp/custom_rtmp_provider.h
  src/providers/facebook/facebook_provider.cpp
  src/providers/facebook/facebook_provider.h
  src/providers/provider.h
  src/providers/provider_base.cpp
  src/providers/provider_base.h
  src/providers/provider_registry.cpp
  src/providers/provider_registry.h
  src/providers/tiktok/tiktok_provider.cpp
  src/providers/tiktok/tiktok_provider.h
  src/providers/twitch/twitch_provider.cpp
  src/providers/twitch/twitch_provider.h
  src/providers/youtube/youtube_provider.cpp
  src/providers/youtube/youtube_provider.h
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
  src/settings/app_config.cpp
  src/settings/app_config.h
  src/settings/config_json.cpp
  src/settings/config_json.h
  src/settings/config_store.cpp
  src/settings/config_store.h
  src/settings/migrations.cpp
  src/settings/migrations.h
  src/utils/clock.cpp
  src/utils/clock.h
  src/utils/i18n.cpp
  src/utils/i18n.h
  src/utils/log.cpp
  src/utils/log.h
  src/utils/paths.cpp
  src/utils/paths.h
  src/utils/strings.cpp
  src/utils/strings.h
  src/utils/uuid.cpp
  src/utils/uuid.h
)

set(RELAYDOCK_PLUGIN_SOURCES src/plugin-main.cpp)

set(RELAYDOCK_TEST_HOOK_SOURCES "")
