# Attest — 라즈베리파이 + 아두이노 2대 통합 테스트

```
                 USB                          USB
  [아두이노 1번] ◀────▶ [라즈베리파이] ◀────▶ [아두이노 2번]
   ├ LED 스트립(WS2812B)    python3 -m hwtest    ├ LCD 16x2 (I2C)
   └ 스테핑 모터(28BYJ-48)                       ├ 도트 매트릭스 8x8 (MAX7219)
                                                ├ 서보 모터 (SG90)
                                                ├ 초음파 (HC-SR04)
                                                ├ 온습도 (DHT11/22)
                                                ├ 조도 (CDS + 10kΩ)
                                                └ RGB LED
```

라즈베리파이가 USB 시리얼(115200bps)로 두 보드에 한 줄짜리 명령을 보내고,
`OK ...` / `ERR ...` 응답을 확인해서 각 부품과 보드 간 연동을 PASS/WARN/FAIL/SKIP 으로 판정합니다.

## 폴더 구성

| 경로 | 내용 |
|---|---|
| `arduino/arduino1_led_stepper/` | 아두이노 1번 펌웨어 |
| `arduino/arduino2_sensors/` | 아두이노 2번 펌웨어 |
| `pi/hwtest/` | 라즈베리파이 테스트 프로그램 (`link`, `discovery`, `suite`, `simulator`) |
| `pi/tests/` | 가상 보드로 테스트 프로그램 자체를 검증하는 pytest |

## 1. 배선 (아두이노 우노 기준)

### 아두이노 1번

| 부품 | 핀 |
|---|---|
| LED 스트립 DIN | D6 (330Ω 직렬 권장) |
| LED 스트립 5V / GND | **외부 5V 전원** (GND 는 아두이노와 공통) |
| ULN2003 IN1 / IN2 / IN3 / IN4 | D8 / D9 / D10 / D11 |
| ULN2003 + / − | 외부 5V / GND |

### 아두이노 2번

| 부품 | 핀 |
|---|---|
| LCD I2C SDA / SCL | A4 / A5 (주소 0x27, 안 보이면 0x3F 로 변경) |
| MAX7219 DIN / CLK / CS | D13 / D10 / D11 |
| 서보 신호 | D9 (빨강: 외부 5V, 검정/갈색: GND) |
| HC-SR04 TRIG / ECHO | D8 / D7 |
| DHT11 DATA | D2 (3핀 모듈: VCC / DATA / GND) |
| 조도 센서 모듈 AO | A0 |
| RGB LED R / G / B | D3 / D5 / D6 (각 220Ω), 공통(가장 긴 다리) → GND (공통 캐소드) |

> LED 스트립·스테핑 모터·서보는 전류가 커서 라즈베리파이 USB 전원만으로는 보드가 리셋될 수 있습니다.
> 외부 5V 전원을 쓰고 GND 를 반드시 공통으로 묶으세요.

LED 개수(`LED_COUNT`), DHT 종류(`DHT_TYPE`), RGB 공통 애노드 여부(`RGB_COMMON_ANODE`)는 각 `.ino` 상단에서 바꿉니다.

## 2. 펌웨어 업로드

라즈베리파이에서 한 번에 (arduino-cli·라이브러리 설치 → 컴파일 → 보드 판별 → 업로드):

```bash
./upload.sh                              # 포트를 모르면 안내에 따라 1번 보드 USB 를 뽑았다 꽂기
./upload.sh /dev/ttyACM0 /dev/ttyACM1    # 1번 포트, 2번 포트를 알고 있으면
```

직접 하려면:

Arduino IDE 라이브러리 매니저에서 설치: **Adafruit NeoPixel, LiquidCrystal I2C, LedControl,
DHT sensor library(Adafruit), Adafruit Unified Sensor, Servo, Stepper**

또는 arduino-cli:

```bash
arduino-cli lib install "Adafruit NeoPixel" "LiquidCrystal I2C" LedControl \
  "DHT sensor library" "Adafruit Unified Sensor" Servo Stepper
arduino-cli compile -b arduino:avr:uno arduino/arduino1_led_stepper
arduino-cli upload  -b arduino:avr:uno -p /dev/ttyACM0 arduino/arduino1_led_stepper
arduino-cli compile -b arduino:avr:uno arduino/arduino2_sensors
arduino-cli upload  -b arduino:avr:uno -p /dev/ttyACM1 arduino/arduino2_sensors
```

## 3. 라즈베리파이에서 실행

```bash
sudo usermod -aG dialout $USER     # 시리얼 권한 (한 번만, 재로그인 필요)
cd pi
pip install -r requirements.txt     # pyserial, pytest

python3 -m hwtest                   # 자동 탐색 + 자동 판정
python3 -m hwtest -i                # 눈으로 확인하는 질문 포함 (처음 조립했을 때 권장)
python3 -m hwtest --only ARD2       # 그룹 선택: ARD1 / ARD2 / 통합
python3 -m hwtest --json report.json
python3 -m hwtest --sim             # 하드웨어 없이 가상 보드로 실행
```

포트 번호(`ttyACM0/1`)는 부팅마다 바뀔 수 있어서, 각 포트에 `PING` 을 보내 `PONG ARD1` / `PONG ARD2`
응답으로 보드를 구분합니다. 직접 지정하려면 `--port1 /dev/ttyACM0 --port2 /dev/ttyACM1`.
실패가 하나라도 있으면 종료 코드 1 입니다.

### 테스트 항목

| 그룹 | 항목 | 자동 판정 기준 |
|---|---|---|
| ARD1 | 통신(PING) | `PONG ARD1` |
| ARD1 | LED 색상 / 픽셀·밝기 | 명령 응답, 범위 밖 인덱스 거부 |
| ARD1 | 스테핑 모터 정/역회전 | 이동 시간이 예상치(512스텝@12rpm ≈ 1.25s)에 근접, 원점 복귀 |
| ARD2 | 통신(PING) | `PONG ARD2` |
| ARD2 | LCD / 도트 매트릭스 / RGB | 명령 응답 (-i 모드에서 육안 확인) |
| ARD2 | 서보 스윕 | 0→90→180→90°, 범위 밖 각도 거부 |
| ARD2 | 초음파 | 7회 중 4회 이상 유효, 2–400cm, 편차 20cm 초과 시 WARN |
| ARD2 | 온습도 | 3회 재시도, −10–60°C / 0–100% |
| ARD2 | 조도 | 0–1023, 끝값(0/1023)에 붙으면 WARN, -i 모드에서 손으로 가려 변화 확인 |
| 통합 | 동시 동작 | ARD1 모터 회전(블로킹) 중에도 ARD2 가 계속 응답 |
| 통합 | 거리 → 서보 + LED | 거리값으로 서보 각도·LED 스트립 색 결정 |
| 통합 | 환경 대시보드 | 온습도·조도 → LCD 표시, RGB(쾌적=초록), 도트(웃음/찡그림), LED 밝기 |
| 통합 | 통신 안정성 | 보드별 PING 50회, 최대 지연 200ms 초과 시 WARN |

LED·LCD·도트·RGB·모터처럼 출력만 있는 부품은 응답만으로는 실제로 켜졌는지 알 수 없으니,
처음에는 `-i` 로 한 번 돌려 눈으로 확인하는 것을 권장합니다.

### 시리얼 프로토콜

`screen /dev/ttyACM0 115200` 등으로 직접 입력해도 됩니다.

| 아두이노 1번 | 아두이노 2번 |
|---|---|
| `PING` → `PONG ARD1` | `PING` → `PONG ARD2` |
| `LED FILL r g b` | `LCD CLEAR`, `LCD PRINT row text` |
| `LED PIX i r g b` | `DOT CLEAR`, `DOT ALL`, `DOT ROW r hex` |
| `LED BRIGHT n`, `LED CLEAR` | `SERVO angle` → `OK SERVO angle` |
| `STEP SPEED rpm` (1–15) | `DIST` → `OK DIST cm` |
| `STEP MOVE n` → 이동 완료 후 `OK STEP MOVE n` | `DHT` → `OK DHT temp hum` |
| `STEP POS` → `OK STEP POS p`, `STEP RELEASE` | `LIGHT` → `OK LIGHT raw`, `RGB r g b` |

## 문제 해결

| 증상 | 확인할 것 |
|---|---|
| 보드를 못 찾음 | `ls /dev/ttyACM*`, dialout 그룹, 시리얼 모니터 등 다른 프로그램이 포트 점유 |
| 모터/서보 동작 시 보드가 재부팅 | 외부 전원 사용, GND 공통 |
| 초음파 `ERR DIST TIMEOUT` | TRIG/ECHO 반대로 꽂힘, 5V 공급 |
| DHT `ERR DHT READ` | DATA 풀업 저항, `DHT_TYPE` 이 실제 센서와 일치하는지 |
| LCD 에 네모만 보임 | I2C 주소(0x27/0x3F), 모듈 뒷면 가변저항(대비) |
| 스테핑 모터가 떨기만 함 | IN1–IN4 순서, `STEP SPEED` 를 낮춰보기 |

## 개발: 가상 보드 테스트

```bash
cd pi && python3 -m pytest -q tests
```

정상 보드, 보드 미연결(SKIP), 센서 고장(FAIL), 무응답(타임아웃 FAIL), 조도값 고착(WARN) 경우를 검증합니다.
