#include "orca/storage/storage.hpp"

#include "esp_littlefs.h"
#include "esp_log.h"
#include "orca/hal/posix_file_system.hpp"

namespace orca::storage {

namespace {
constexpr const char* TAG = "storage";
hal::PosixFileSystem g_data("/data");
}  // namespace

esp_err_t mount() {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/data";
  conf.partition_label = "storage";
  conf.format_if_mount_failed = true;
  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
    return err;
  }
  size_t total = 0, used = 0;
  esp_littlefs_info("storage", &total, &used);
  ESP_LOGI(TAG, "LittleFS /data: %u KB used of %u KB", static_cast<unsigned>(used / 1024),
           static_cast<unsigned>(total / 1024));
  return ESP_OK;
}

hal::FileSystem& data() { return g_data; }

}  // namespace orca::storage
