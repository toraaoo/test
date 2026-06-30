#pragma once

namespace desktop::app {

// Launch the resident tray helper (colocated with this executable) if it isn't
// already running; its single-instance guard makes a redundant launch a no-op.
void SpawnTray();

}  // namespace desktop::app
