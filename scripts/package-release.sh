#!/usr/bin/env bash
set -euo pipefail

version="${1:?usage: package-release.sh VERSION}"
environment="cardputer-adv-recorder"
build_dir=".pio/build/${environment}"
output_dir="dist"
prefix="${output_dir}/cardputer-adv-recorder-${version}"
platformio_home="${PLATFORMIO_CORE_DIR:-${HOME}/.platformio}"
boot_app0="${platformio_home}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
esptool="${platformio_home}/packages/tool-esptoolpy/esptool.py"
platformio_command="$(command -v platformio || command -v pio || true)"
if [[ -n "${platformio_command}" &&
      -x "$(dirname "${platformio_command}")/python" ]]; then
  platformio_python="$(dirname "${platformio_command}")/python"
elif [[ -x "${platformio_home}/penv/bin/python" ]]; then
  platformio_python="${platformio_home}/penv/bin/python"
else
  echo "Could not find the PlatformIO Python interpreter." >&2
  exit 1
fi

mkdir -p "${output_dir}"
cp "${build_dir}/firmware.bin" "${prefix}-firmware.bin"
cp "${build_dir}/bootloader.bin" "${prefix}-bootloader.bin"
cp "${build_dir}/partitions.bin" "${prefix}-partitions.bin"
cp "${boot_app0}" "${prefix}-boot_app0.bin"

"${platformio_python}" "${esptool}" --chip esp32s3 merge_bin \
  -o "${prefix}-complete.bin" \
  0x0000 "${build_dir}/bootloader.bin" \
  0x8000 "${build_dir}/partitions.bin" \
  0xe000 "${boot_app0}" \
  0x10000 "${build_dir}/firmware.bin"

cat > "${prefix}-flash.txt" <<EOF
Cardputer ADV Recorder ${version}

PlatformIO:
  platformio run -e cardputer-adv-recorder --target upload

esptool:
  esptool.py --chip esp32s3 --baud 460800 write_flash \\
    0x0000 $(basename "${prefix}")-bootloader.bin \\
    0x8000 $(basename "${prefix}")-partitions.bin \\
    0xe000 $(basename "${prefix}")-boot_app0.bin \\
    0x10000 $(basename "${prefix}")-firmware.bin

Complete image:
  esptool.py --chip esp32s3 --baud 460800 write_flash \\
    0x0000 $(basename "${prefix}")-complete.bin
EOF

(
  cd "${output_dir}"
  shasum -a 256 \
    "$(basename "${prefix}")-firmware.bin" \
    "$(basename "${prefix}")-bootloader.bin" \
    "$(basename "${prefix}")-partitions.bin" \
    "$(basename "${prefix}")-boot_app0.bin" \
    "$(basename "${prefix}")-complete.bin" \
    > "$(basename "${prefix}")-SHA256SUMS.txt"
)
