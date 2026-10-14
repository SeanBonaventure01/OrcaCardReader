#!/usr/bin/env bash
# Fails if a portable (★) component includes ESP-IDF or FreeRTOS headers.
# The list of portable components is in tools/portable_components.txt.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
components="$root/firmware/components"
pattern='#\s*include\s*[<"](esp_|freertos/|driver/|hal/[a-z_]+_ll|soc/|sdkconfig|nvs|lwip/|lvgl)'

status=0
while read -r comp; do
  [[ -z "$comp" || "$comp" == \#* ]] && continue
  dir="$components/$comp"
  [[ -d "$dir" ]] || { echo "check_portable: missing component '$comp'"; status=1; continue; }
  if hits=$(grep -rnE "$pattern" "$dir" --include='*.hpp' --include='*.h' --include='*.cpp' --include='*.c'); then
    echo "check_portable: platform include in portable component '$comp':"
    echo "$hits"
    status=1
  fi
done < "$root/tools/portable_components.txt"

[[ $status -eq 0 ]] && echo "check_portable: OK"
exit $status
