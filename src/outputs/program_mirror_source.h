// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
#pragma once

namespace rd {

// Id of the helper source type RelayDock registers with OBS.
inline constexpr const char *kProgramMirrorSourceId = "relaydock_program_mirror";

// Registers a source that shows what OBS has in Program.
//
// It draws the picture OBS already rendered for the main output. It does not render your
// scene a second time, so putting Program on the vertical canvas costs one extra draw call.
// It follows scene switches and transitions by itself, because it shows the finished frame.
//
// The source is hidden from the OBS "Add Source" menu. Only RelayDock's vertical canvas uses it.
// Call once from obs_module_load.
void registerProgramMirrorSource();

} // namespace rd
