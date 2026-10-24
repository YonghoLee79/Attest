#!/usr/bin/env bash
# 라즈베리파이에서 두 아두이노에 테스트 펌웨어를 업로드한다.
#
#   ./upload.sh                                 # 포트 자동 판별(필요하면 USB 를 뽑았다 꽂으라고 안내)
#   ./upload.sh /dev/ttyACM0 /dev/ttyACM1       # 1번 포트, 2번 포트 직접 지정
#   FQBN=arduino:avr:nano ./upload.sh           # 우노가 아닌 보드 (이 경우 직접 컴파일)
#   BUILD=1 ./upload.sh                         # .ino 를 고친 뒤 직접 컴파일해서 올릴 때
#
# 우노는 firmware/ 에 미리 컴파일한 .hex 를 그대로 올린다(라즈베리파이 컴파일은 매우 느림).
set -euo pipefail

FQBN="${FQBN:-arduino:avr:uno}"
DIR="$(cd "$(dirname "$0")" && pwd)"
SKETCH1="$DIR/arduino/arduino1_led_stepper"
SKETCH2="$DIR/arduino/arduino2_sensors"
HEX1="$DIR/firmware/arduino1_led_stepper.uno.hex"
HEX2="$DIR/firmware/arduino2_sensors.uno.hex"
PREBUILT=0
[[ "$FQBN" == "arduino:avr:uno" && -z "${BUILD:-}" && -f "$HEX1" && -f "$HEX2" ]] && PREBUILT=1
export PATH="$HOME/.local/bin:$PATH"

list_ports() { ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true; }

# 포트 개수가 $1 개가 될 때까지 최대 $2 초 기다린다.
wait_port_count() {
  local want=$1 limit=$2 i n
  for ((i = 0; i < limit * 2; i++)); do
    n=$(list_ports | wc -l)
    (( n == want )) && return 0
    sleep 0.5
  done
  return 1
}

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

echo "[2/4] AVR 코어 확인"
# arduino-cli core list 는 라즈베리파이에서 수십 초 걸리므로 설치 폴더만 확인한다.
if ! ls -d "$HOME"/.arduino15/packages/arduino/hardware/avr/*/ >/dev/null 2>&1; then
  echo "  AVR 코어 설치 중... (처음 한 번, 몇 분 걸립니다)"
  arduino-cli core update-index
  arduino-cli core install arduino:avr
fi
if (( PREBUILT )); then
  echo "[3/4] 미리 컴파일된 펌웨어 사용 (firmware/*.uno.hex) - 컴파일 생략"
else
# 라즈베리파이에서는 lib install 한 번에 수십 초가 걸리므로, 설치된 목록을 한 번만 읽고 없는 것만 설치한다.
installed=$(arduino-cli lib list --format json)
missing=()
for lib in "Adafruit NeoPixel" "LiquidCrystal I2C" "LedControl" \
           "DHT sensor library" "Adafruit Unified Sensor" "Servo" "Stepper"; do
  grep -qF "\"name\": \"$lib\"" <<<"$installed" || missing+=("$lib")
done
if (( ${#missing[@]} )); then
  echo "  라이브러리 설치 중: ${missing[*]} (1~2분 걸릴 수 있습니다)"
  arduino-cli lib install "${missing[@]}"
else
  echo "  라이브러리 모두 설치됨"
fi

# ---- 2. 컴파일 (업로드 전에 오류를 먼저 확인) ----
echo "[3/4] 컴파일 ($FQBN)"
for s in "$SKETCH1" "$SKETCH2"; do
  if ! out=$(arduino-cli compile -b "$FQBN" "$s" 2>&1); then
    echo "$out"; echo "[!] 컴파일 실패: $s"; exit 1
  fi
  echo "  $(basename "$s"): $(grep -o 'Sketch uses [0-9]* bytes ([0-9]*%)' <<<"$out")"
done
fi

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
  # Enter 대신 포트 개수 변화를 감지한다(미리 눌린 키 입력에 영향받지 않음).
  echo ">> 1번 보드(LED 스트립 + 스테핑 모터)의 USB 케이블을 뽑으세요... (60초 대기)"
  if ! wait_port_count 1 60; then
    echo "[!] 60초 안에 포트가 1개로 줄지 않았습니다. 다시 실행하세요."
    exit 1
  fi
  mapfile -t LEFT < <(list_ports)
  PORT2="${LEFT[0]}"
  echo "   감지됨: 남은 포트 $PORT2 = 2번 보드"
  echo ">> 이제 1번 보드를 다시 꽂으세요... (60초 대기)"
  if ! wait_port_count 2 60; then
    echo "[!] 60초 안에 1번 보드가 다시 잡히지 않았습니다. 케이블을 확인하고 다시 실행하세요."
    exit 1
  fi
  sleep 2  # 꽂은 직후 포트가 안정될 때까지
  PORT1=""
  for p in $(list_ports); do [[ "$p" != "$PORT2" ]] && PORT1="$p"; done
  if [[ -z "$PORT1" ]]; then
    echo "[!] 1번 보드 포트를 찾지 못했습니다. 다시 실행하세요."
    exit 1
  fi
fi
echo "1번 보드: $PORT1"
echo "2번 보드: $PORT2"

# 다른 프로그램이 포트를 잡고 있으면 업로드 응답을 가로채 실패하므로 미리 알린다.
busy=$(fuser "$PORT1" "$PORT2" 2>/dev/null | xargs -n1 2>/dev/null | sort -u || true)
if [[ -n "$busy" ]]; then
  echo "[!] 다른 프로그램이 포트를 사용 중입니다:"
  ps -o pid,cmd -p $busy
  echo "    해당 프로그램을 멈춘 뒤 다시 실행하세요. 예) kill $busy  (서비스라면 sudo systemctl stop 서비스명)"
  exit 1
fi

# ---- 4. 업로드 ----
echo "[4/4] 업로드"
# 미리 컴파일된 hex 는 avrdude 로 바로 올린다(arduino-cli upload 는 시작만 수십 초 걸림).
AVRDUDE=$(ls -d "$HOME"/.arduino15/packages/arduino/tools/avrdude/*/ 2>/dev/null | tail -1)
flash() {  # flash 포트 hex|스케치
  if (( PREBUILT )) && [[ -n "$AVRDUDE" ]]; then
    # 우노 부트로더는 115200, 나노(구형 부트로더) 호환보드는 57600. 칩이 같아서 같은 hex 를 쓴다.
    local baud
    for baud in 115200 57600; do
      echo "    avrdude ${baud}bps 시도"
      if "$AVRDUDE/bin/avrdude" -C "$AVRDUDE/etc/avrdude.conf" -p atmega328p -c arduino \
           -P "$1" -b "$baud" -D -U "flash:w:$2:i" -q -q; then
        return 0
      fi
    done
    echo "[!] $1 업로드 실패. 보드 종류(UNO/NANO 등)와 케이블을 확인하세요."
    return 1
  elif (( PREBUILT )); then
    arduino-cli upload -b "$FQBN" -p "$1" -i "$2"
  else
    arduino-cli upload -b "$FQBN" -p "$1" "$2"
  fi
}
if (( PREBUILT )); then SRC1="$HEX1"; SRC2="$HEX2"; else SRC1="$SKETCH1"; SRC2="$SKETCH2"; fi
echo "  1번 보드 ($PORT1)"
flash "$PORT1" "$SRC1"
echo "  1번 보드 완료"
echo "  2번 보드 ($PORT2)"
flash "$PORT2" "$SRC2"
echo "  2번 보드 완료"

echo
echo "업로드 끝. 테스트 실행:"
echo "  cd $DIR/pi && python3 -m hwtest -i --json report.json"
