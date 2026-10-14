#pragma once

#include "esp_err.h"
#include "orca/hal/file_system.hpp"

namespace orca::storage {

// Mounts LittleFS (partition "storage") at /data, formatting it on first boot.
// Holds config.json, settings.json and the game snapshot until the SD card is fitted.
esp_err_t mount();

hal::FileSystem& data();  // paths are relative to /data, e.g. "/config.json"

}  // namespace orca::storage
