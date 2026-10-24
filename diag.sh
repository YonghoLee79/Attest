#!/usr/bin/env bash
# 업로드가 안 될 때 원인 진단. 보드에 아무것도 쓰지 않는다(칩 서명만 읽음).
#   ./diag.sh [포트 ...]
PORTS=("$@")
(( ${#PORTS[@]} )) || mapfile -t PORTS < <(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null)
AVRDUDE=$(ls -d "$HOME"/.arduino15/packages/arduino/tools/avrdude/*/ 2>/dev/null | tail -1)

echo "== USB 장치"
lsusb 2>/dev/null | grep -viE "hub|root" || echo "(lsusb 없음)"
echo
echo "== 포트"
ls -l /dev/serial/by-id/ 2>/dev/null || echo "(by-id 없음)"
echo "groups: $(id -nG)"
echo
echo "== 포트를 사용 중인 프로세스"
for p in "${PORTS[@]}"; do
  users=$(fuser "$p" 2>/dev/null)
  if [[ -n "$users" ]]; then ps -o pid,cmd -p $users; else echo "$p: 없음"; fi
done
echo
echo "== 최근 커널 메시지"
dmesg 2>/dev/null | grep -iE "ttyUSB|ttyACM|ch34|ftdi|cp210|brltty" | tail -8 || echo "(dmesg 권한 없음)"

[[ -z "$AVRDUDE" ]] && { echo "[!] avrdude 없음 - upload.sh 를 먼저 실행하세요"; exit 1; }
# 보드 종류별로 칩 서명 읽기 시도: 우노/나노(새 부트로더), 나노(구형), 메가
for p in "${PORTS[@]}"; do
  echo
  echo "== $p 칩 확인"
  for spec in "atmega328p arduino 115200 UNO/나노(새)" "atmega328p arduino 57600 나노(구형)" \
              "atmega2560 wiring 115200 메가2560" "atmega168 arduino 19200 구형168"; do
    read -r part prog baud name <<<"$spec"
    out=$(timeout 15 "$AVRDUDE/bin/avrdude" -C "$AVRDUDE/etc/avrdude.conf" \
          -p "$part" -c "$prog" -P "$p" -b "$baud" -D 2>&1)
    if grep -qi "signature =" <<<"$out" && ! grep -qiE "mismatch|not in sync" <<<"$out"; then
      echo "  [OK] $name ($part, ${baud}bps): $(grep -i "signature =" <<<"$out" | head -1 | sed "s/^.*signature = //I")"
      break
    else
      echo "  [--] $name: $(grep -iE 'error|not in sync|mismatch|resp=|cannot' <<<"$out" | head -1)"
    fi
  done
done
