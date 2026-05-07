#!/bin/bash
# 下载 Bosch BMI270-Sensor-API 核心文件到项目 lib 目录

set -e

BASE="https://raw.githubusercontent.com/bxhsiman/esp32-component-bmi270/main/bmi270_sensor_api"
DEST="lib/bmi270_sensor_api"

mkdir -p "$DEST"

echo "Downloading BMI270-Sensor-API core files..."

curl -sL "$BASE/bmi2.h"   -o "$DEST/bmi2.h"
curl -sL "$BASE/bmi2.c"   -o "$DEST/bmi2.c"
curl -sL "$BASE/bmi2_defs.h" -o "$DEST/bmi2_defs.h"
curl -sL "$BASE/bmi270.h" -o "$DEST/bmi270.h"
curl -sL "$BASE/bmi270.c" -o "$DEST/bmi270.c"

echo "Done. Files in $DEST:"
ls -la "$DEST"
