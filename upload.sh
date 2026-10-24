#!/usr/bin/env bash
# 라즈베리파이에서 두 아두이노에 테스트 펌웨어를 업로드한다.
#
#   ./upload.sh                                 # 포트 자동 판별(필요하면 USB 를 뽑았다 꽂으라고 안내)
#   ./upload.sh /dev/ttyACM0 /dev/ttyACM1       # 1번 포트, 2번 포트 직접 지정
#   FQBN=arduino:avr:nano ./upload.sh           # 우노가 아닌 보드
#
# arduino-cli 와 필요한 라이브러리가 없으면 자동으로 설치한다.
set -euo pipefail

FQBN="${FQBN:-arduino:avr:uno}"
DIR="$(cd "$(dirname "$0")" && pwd)"
SKETCH1="$DIR/arduino/arduino1_led_stepper"
SKETCH2="$DIR/arduino/arduino2_sensors"
export PATH="$HOME/.local/bin:$PATH"

list_ports() { ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true; }

# ---- 1. arduino-cli / 코어 / 라이브러리 ----
if ! command -v arduino-cli >/dev/null; then
  echo "[1/4] arduino-cli 설치"
  case "$(uname -m)" in
    aarch64|arm64) ARCH=ARM64 ;;
    armv7l)        ARCH=ARMv7 ;;
    armv6l)        ARCH=ARMv6 ;;
    x86_64)        ARCH=64bit ;;
    *) echo "[!] 지원하지 않는 CPU: $(uname -m)"; exit 1 ;;
  esac
  mkdir -p "$HOME/.local/bin"
  curl -fsSL "https://github.com/arduino/arduino-cli/releases/download/v1.1.1/arduino-cli_1.1.1_Linux_${ARCH}.tar.gz" \
    | tar xz -C "$HOME/.local/bin" arduino-cli
else
  echo "[1/4] arduino-cli 있음: $(arduino-cli version | head -1)"
fi

echo "[2/4] AVR 코어 / 라이브러리 확인"
arduino-cli core update-index >/dev/null
arduino-cli core list | grep -q '^arduino:avr' || arduino-cli core install arduino:avr
arduino-cli lib install "Adafruit NeoPixel" "LiquidCrystal I2C" "LedControl" \
  "DHT sensor library" "Adafruit Unified Sensor" "Servo" "Stepper" >/dev/null

# ---- 2. 컴파일 (업로드 전에 오류를 먼저 확인) ----
echo "[3/4] 컴파일 ($FQBN)"
for s in "$SKETCH1" "$SKETCH2"; do
  if ! out=$(arduino-cli compile -b "$FQBN" "$s" 2>&1); then
    echo "$out"; echo "[!] 컴파일 실패: $s"; exit 1
  fi
  echo "  $(basename "$s"): $(grep -o 'Sketch uses [0-9]* bytes ([0-9]*%)' <<<"$out")"
done

# ---- 3. 어느 포트가 몇 번 보드인지 ----
PORT1="${1:-}"
PORT2="${2:-}"
if [[ -z "$PORT1" || -z "$PORT2" ]]; then
  mapfile -t PORTS < <(list_ports)
  if (( ${#PORTS[@]} != 2 )); then
    echo "[!] USB 시리얼 포트가 ${#PORTS[@]}개 보입니다: ${PORTS[*]:-(없음)}"
    echo "    두 아두이노를 모두 꽂은 뒤 다시 실행하거나, 포트를 직접 지정하세요: ./upload.sh 1번포트 2번포트"
    exit 1
  fi
  echo
  echo "포트 2개 발견: ${PORTS[*]}"
  echo ">> 1번 보드(LED 스트립 + 스테핑 모터)의 USB 케이블만 뽑고 Enter 를 누르세요."
  read -r
  sleep 1
  mapfile -t LEFT < <(list_ports)
  if (( ${#LEFT[@]} != 1 )); then
    echo "[!] 남은 포트가 ${#LEFT[@]}개입니다(${LEFT[*]:-없음}). 한 개만 뽑고 다시 실행하세요."
    exit 1
  fi
  PORT2="${LEFT[0]}"
  echo ">> 이제 1번 보드를 다시 꽂고 Enter 를 누르세요."
  read -r
  sleep 2
  PORT1=""
  for p in $(list_ports); do [[ "$p" != "$PORT2" ]] && PORT1="$p"; done
  if [[ -z "$PORT1" ]]; then
    echo "[!] 1번 보드 포트를 찾지 못했습니다. 다시 꽂았는지 확인하세요."
    exit 1
  fi
fi
echo "1번 보드: $PORT1"
echo "2번 보드: $PORT2"

# ---- 4. 업로드 ----
echo "[4/4] 업로드"
arduino-cli upload -b "$FQBN" -p "$PORT1" "$SKETCH1"
echo "  1번 보드 완료"
arduino-cli upload -b "$FQBN" -p "$PORT2" "$SKETCH2"
echo "  2번 보드 완료"

echo
echo "업로드 끝. 테스트 실행:"
echo "  cd $DIR/pi && python3 -m hwtest -i --json report.json"
