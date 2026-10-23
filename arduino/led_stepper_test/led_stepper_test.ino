// LED 스트립(WS2812B/NeoPixel) + 스텝 모터 하드웨어 테스트 스케치
//
// 필요 라이브러리: Adafruit NeoPixel (라이브러리 매니저에서 설치)
// 시리얼 모니터: 115200 baud, 줄 끝 "Newline" 으로 설정 후 'h' 입력 → 명령 목록
//
// 아래 "설정" 부분만 실제 배선에 맞게 고치면 됩니다.

#include <Adafruit_NeoPixel.h>

// ===================== 설정 =====================

// --- LED 스트립 ---
#define LED_PIN        6     // 데이터 핀 (330Ω 직렬 저항 권장)
#define NUM_LEDS       30    // LED 개수
#define LED_BRIGHTNESS 50    // 0~255. USB 전원으로 테스트할 땐 낮게 유지

// --- 스텝 모터 드라이버 종류 (하나만 선택) ---
#define DRIVER_STEP_DIR 1    // A4988 / DRV8825 / TMC2208 등 (STEP/DIR 핀)
#define DRIVER_ULN2003  2    // 28BYJ-48 + ULN2003 보드 (IN1~IN4 핀)
#define STEPPER_DRIVER  DRIVER_STEP_DIR

#if STEPPER_DRIVER == DRIVER_STEP_DIR
  #define STEP_PIN       3
  #define DIR_PIN        4
  #define EN_PIN         5     // 드라이버 EN 핀 (LOW = 활성). 연결 안 했으면 -1
  #define STEPS_PER_REV  200   // 1.8° 모터 풀스텝 기준. 마이크로스텝이면 곱해주기 (예: 1/16 → 3200)
  #define DEFAULT_SPEED  400   // steps/sec
#elif STEPPER_DRIVER == DRIVER_ULN2003
  #define IN1_PIN        8
  #define IN2_PIN        9
  #define IN3_PIN        10
  #define IN4_PIN        11
  #define STEPS_PER_REV  4096  // 28BYJ-48 하프스텝 기준
  #define DEFAULT_SPEED  600   // steps/sec (28BYJ-48 은 ~1000 이상이면 탈조)
#else
  #error "STEPPER_DRIVER 값을 DRIVER_STEP_DIR 또는 DRIVER_ULN2003 으로 설정하세요"
#endif

// ================================================

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------- 스텝 모터 (논블로킹) ----------

long stepsRemaining = 0;     // 남은 스텝 수 (부호 = 방향)
long position = 0;           // 현재 위치 (스텝)
unsigned int stepSpeed = DEFAULT_SPEED;
unsigned long lastStepUs = 0;

#if STEPPER_DRIVER == DRIVER_ULN2003
const uint8_t coilPins[4] = {IN1_PIN, IN2_PIN, IN3_PIN, IN4_PIN};
const uint8_t halfStepSeq[8][4] = {
  {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
  {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1},
};
uint8_t seqIndex = 0;
#endif

void motorEnable(bool on) {
#if STEPPER_DRIVER == DRIVER_STEP_DIR
  if (EN_PIN >= 0) digitalWrite(EN_PIN, on ? LOW : HIGH);
#else
  if (!on) {
    // 코일 전류 차단 (발열 방지)
    for (uint8_t i = 0; i < 4; i++) digitalWrite(coilPins[i], LOW);
  }
#endif
}

void motorSetup() {
#if STEPPER_DRIVER == DRIVER_STEP_DIR
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  if (EN_PIN >= 0) pinMode(EN_PIN, OUTPUT);
#else
  for (uint8_t i = 0; i < 4; i++) pinMode(coilPins[i], OUTPUT);
#endif
  motorEnable(false);
}

void motorStepOnce(bool forward) {
#if STEPPER_DRIVER == DRIVER_STEP_DIR
  digitalWrite(DIR_PIN, forward ? HIGH : LOW);
  delayMicroseconds(2);
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(3);
  digitalWrite(STEP_PIN, LOW);
#else
  seqIndex = (seqIndex + (forward ? 1 : 7)) % 8;
  for (uint8_t i = 0; i < 4; i++) digitalWrite(coilPins[i], halfStepSeq[seqIndex][i]);
#endif
  position += forward ? 1 : -1;
}

void motorMove(long steps) {
  if (steps == 0) return;
  stepsRemaining = steps;
  motorEnable(true);
  lastStepUs = micros();
}

void motorStop() {
  stepsRemaining = 0;
  motorEnable(false);
}

void motorUpdate() {
  if (stepsRemaining == 0) return;
  unsigned long now = micros();
  if (now - lastStepUs < 1000000UL / stepSpeed) return;
  lastStepUs = now;

  bool forward = stepsRemaining > 0;
  motorStepOnce(forward);
  stepsRemaining += forward ? -1 : 1;

  if (stepsRemaining == 0) {
    motorEnable(false);
    Serial.print(F("[모터] 이동 완료, 위치 = "));
    Serial.println(position);
  }
}

// ---------- LED 스트립 ----------

enum LedMode { LED_STATIC, LED_RAINBOW, LED_CHASE, LED_WALK };
LedMode ledMode = LED_STATIC;
uint16_t ledFrame = 0;
unsigned long lastLedMs = 0;

void ledFill(uint8_t r, uint8_t g, uint8_t b) {
  ledMode = LED_STATIC;
  strip.fill(strip.Color(r, g, b));
  strip.show();
}

void ledUpdate() {
  if (ledMode == LED_STATIC) return;
  unsigned long now = millis();
  if (now - lastLedMs < (ledMode == LED_WALK ? 100 : 20)) return;
  lastLedMs = now;

  switch (ledMode) {
    case LED_RAINBOW:
      for (uint16_t i = 0; i < NUM_LEDS; i++) {
        uint16_t hue = (ledFrame * 256) + (i * 65536UL / NUM_LEDS);
        strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
      }
      break;
    case LED_CHASE:
      strip.clear();
      for (uint16_t i = (ledFrame / 3) % 3; i < NUM_LEDS; i += 3) {
        strip.setPixelColor(i, strip.Color(255, 120, 0));
      }
      break;
    case LED_WALK:
      // LED 하나씩 켜서 끊긴 픽셀/개수 확인
      strip.clear();
      strip.setPixelColor(ledFrame % NUM_LEDS, strip.Color(255, 255, 255));
      break;
    default:
      break;
  }
  strip.show();
  ledFrame++;
}

// ---------- 자동 테스트 ----------

void autoTest() {
  Serial.println(F("=== 자동 테스트 시작 ==="));

  Serial.println(F("[LED] 빨강 → 초록 → 파랑 → 흰색"));
  ledFill(255, 0, 0);     delay(700);
  ledFill(0, 255, 0);     delay(700);
  ledFill(0, 0, 255);     delay(700);
  ledFill(255, 255, 255); delay(700);

  Serial.println(F("[LED] 픽셀 하나씩 점등 (끊긴 LED 확인)"));
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    strip.clear();
    strip.setPixelColor(i, strip.Color(0, 0, 255));
    strip.show();
    delay(40);
  }
  ledFill(0, 0, 0);

  Serial.println(F("[모터] 정방향 1바퀴"));
  motorMove(STEPS_PER_REV);
  while (stepsRemaining != 0) motorUpdate();
  delay(500);

  Serial.println(F("[모터] 역방향 1바퀴"));
  motorMove(-STEPS_PER_REV);
  while (stepsRemaining != 0) motorUpdate();

  Serial.println(F("[동시] 모터 반 바퀴 왕복 + 무지개 LED"));
  ledMode = LED_RAINBOW;
  motorMove(STEPS_PER_REV / 2);
  while (stepsRemaining != 0) { motorUpdate(); ledUpdate(); }
  motorMove(-STEPS_PER_REV / 2);
  while (stepsRemaining != 0) { motorUpdate(); ledUpdate(); }
  ledFill(0, 0, 0);

  Serial.println(F("=== 자동 테스트 완료 ==="));
}

// ---------- 시리얼 명령 ----------

void printHelp() {
  Serial.println(F("--- 명령 목록 ---"));
  Serial.println(F(" t          : 자동 테스트 (LED + 모터)"));
  Serial.println(F(" r / g / b / w : LED 빨강/초록/파랑/흰색"));
  Serial.println(F(" o          : LED 끄기"));
  Serial.println(F(" a          : 무지개 애니메이션"));
  Serial.println(F(" c          : 체이스 애니메이션"));
  Serial.println(F(" k          : LED 하나씩 이동 (불량 픽셀 확인)"));
  Serial.println(F(" B <0-255>  : 밝기 설정"));
  Serial.println(F(" m <스텝>   : 모터 이동 (음수 = 역방향), 예) m 200, m -200"));
  Serial.println(F(" R <바퀴>   : 모터 N바퀴 회전 (음수 = 역방향)"));
  Serial.println(F(" s <속도>   : 모터 속도 steps/sec"));
  Serial.println(F(" x          : 모터 정지 + 전원 해제"));
  Serial.println(F(" p          : 현재 상태 출력"));
  Serial.println(F(" h          : 도움말"));
}

void printStatus() {
  Serial.print(F("LED 개수=")); Serial.print(NUM_LEDS);
  Serial.print(F(", 밝기="));   Serial.print(strip.getBrightness());
  Serial.print(F(" | 모터 위치=")); Serial.print(position);
  Serial.print(F(", 남은 스텝=")); Serial.print(stepsRemaining);
  Serial.print(F(", 속도="));   Serial.print(stepSpeed);
  Serial.print(F(" steps/s, 1바퀴=")); Serial.print(STEPS_PER_REV);
  Serial.println(F(" steps"));
}

void handleCommand(char *line) {
  while (*line == ' ') line++;
  if (*line == '\0') return;
  char cmd = line[0];
  long arg = atol(line + 1);

  switch (cmd) {
    case 't': autoTest(); break;
    case 'r': ledFill(255, 0, 0); break;
    case 'g': ledFill(0, 255, 0); break;
    case 'b': ledFill(0, 0, 255); break;
    case 'w': ledFill(255, 255, 255); break;
    case 'o': ledFill(0, 0, 0); break;
    case 'a': ledMode = LED_RAINBOW; break;
    case 'c': ledMode = LED_CHASE; break;
    case 'k': ledMode = LED_WALK; ledFrame = 0; break;
    case 'B':
      strip.setBrightness(constrain(arg, 0, 255));
      strip.show();
      break;
    case 'm': motorMove(arg); break;
    case 'R': motorMove(arg * STEPS_PER_REV); break;
    case 's':
      if (arg > 0) stepSpeed = arg;
      break;
    case 'x': motorStop(); Serial.println(F("[모터] 정지")); break;
    case 'p': printStatus(); break;
    case 'h': printHelp(); return;
    default:
      Serial.print(F("알 수 없는 명령: "));
      Serial.println(cmd);
      return;
  }
  Serial.print(F("> "));
  Serial.println(line);
}

void readSerial() {
  static char buf[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[len] = '\0';
      handleCommand(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// ---------- 메인 ----------

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show();
  motorSetup();

  Serial.println(F("LED 스트립 + 스텝 모터 테스트"));
  printHelp();
}

void loop() {
  readSerial();
  motorUpdate();
  ledUpdate();
}
