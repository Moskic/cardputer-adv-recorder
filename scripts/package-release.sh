#!/usr/bin/env bash
set -euo pipefail

version="${1:?usage: package-release.sh VERSION}"
environment="cardputer-adv-recorder"
build_dir=".pio/build/${environment}"
output_dir="dist"
image_name="cardputer-adv-recorder-${version}.bin"
image_path="${output_dir}/${image_name}"
platformio_home="${PLATFORMIO_CORE_DIR:-${HOME}/.platformio}"
boot_app0="${platformio_home}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
esptool="${platformio_home}/packages/tool-esptoolpy/esptool.py"
platformio_command="$(command -v platformio || command -v pio || true)"
if [[ -n "${platformio_command}" &&
      -x "$(dirname "${platformio_command}")/python" ]]; then
  platformio_python="$(dirname "${platformio_command}")/python"
elif [[ -x ".venv/bin/python" ]]; then
  platformio_python=".venv/bin/python"
elif [[ -x "${platformio_home}/penv/bin/python" ]]; then
  platformio_python="${platformio_home}/penv/bin/python"
else
  echo "Could not find the PlatformIO Python interpreter." >&2
  exit 1
fi

mkdir -p "${output_dir}"

"${platformio_python}" "${esptool}" --chip esp32s3 merge_bin \
  -o "${image_path}" \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 8MB \
  0x0000 "${build_dir}/bootloader.bin" \
  0x8000 "${build_dir}/partitions.bin" \
  0xe000 "${boot_app0}" \
  0x10000 "${build_dir}/firmware.bin"

(
  cd "${output_dir}"
  shasum -a 256 "${image_name}" > SHA256SUMS.txt
)
