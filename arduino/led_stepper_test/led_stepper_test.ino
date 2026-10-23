// 단색 LED 스트립(MOSFET 구동) + 28BYJ-48/ULN2003 스텝 모터 테스트 스케치
//
// 라이브러리 필요 없음
// 시리얼 모니터: 115200 baud, 줄 끝 "LF" 로 설정 후 'h' 입력 → 명령 목록
//
// 아래 "설정" 부분만 실제 배선에 맞게 고치면 됩니다.

// ===================== 설정 =====================

// --- 단색 LED 스트립 (MOSFET 보드 게이트/SIG 핀) ---
// PWM 가능한 핀이어야 밝기 조절이 됨 (Uno: 3, 5, 6, 9, 10, 11)
#define LED_PIN        6
#define LED_BRIGHTNESS 128   // 0~255 시작 밝기

// --- 28BYJ-48 + ULN2003 ---
#define IN1_PIN        8
#define IN2_PIN        9
#define IN3_PIN        10
#define IN4_PIN        11
#define STEPS_PER_REV  4096  // 하프스텝 기준
#define DEFAULT_SPEED  400   // steps/sec (~800 이상이면 탈조 가능)

// ================================================

// ---------- 스텝 모터 (논블로킹) ----------

long stepsRemaining = 0;     // 남은 스텝 수 (부호 = 방향)
long position = 0;           // 현재 위치 (스텝)
unsigned int stepSpeed = DEFAULT_SPEED;
unsigned long lastStepUs = 0;

const uint8_t coilPins[4] = {IN1_PIN, IN2_PIN, IN3_PIN, IN4_PIN};
const uint8_t halfStepSeq[8][4] = {
  {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
  {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1},
};
uint8_t seqIndex = 0;

void motorRelease() {
  // 코일 전류 차단 (발열 방지)
  for (uint8_t i = 0; i < 4; i++) digitalWrite(coilPins[i], LOW);
}

void motorSetup() {
  for (uint8_t i = 0; i < 4; i++) pinMode(coilPins[i], OUTPUT);
  motorRelease();
}

void motorStepOnce(bool forward) {
  seqIndex = (seqIndex + (forward ? 1 : 7)) % 8;
  for (uint8_t i = 0; i < 4; i++) digitalWrite(coilPins[i], halfStepSeq[seqIndex][i]);
  position += forward ? 1 : -1;
}

void motorMove(long steps) {
  if (steps == 0) return;
  stepsRemaining = steps;
  lastStepUs = micros();
}

void motorStop() {
  stepsRemaining = 0;
  motorRelease();
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
    motorRelease();
    Serial.print(F("[모터] 이동 완료, 위치 = "));
    Serial.println(position);
  }
}

// 배선 점검: IN1~IN4 를 하나씩 1초간 켬.
// ULN2003 보드의 LED A~D 가 순서대로 켜져야 정상.
void coilTest() {
  stepsRemaining = 0;
  for (uint8_t i = 0; i < 4; i++) {
    motorRelease();
    digitalWrite(coilPins[i], HIGH);
    Serial.print(F("[코일] IN"));
    Serial.print(i + 1);
    Serial.print(F(" (D"));
    Serial.print(coilPins[i]);
    Serial.println(F(") ON"));
    delay(1000);
  }
  motorRelease();
  Serial.println(F("[코일] 테스트 완료"));
}

// ---------- 단색 LED 스트립 ----------

enum LedMode { LED_STATIC, LED_BREATHE, LED_BLINK };
LedMode ledMode = LED_STATIC;
uint8_t ledLevel = LED_BRIGHTNESS;   // 켤 때 밝기
uint16_t ledFrame = 0;
unsigned long lastLedMs = 0;

void ledSet(uint8_t level) {
  ledMode = LED_STATIC;
  analogWrite(LED_PIN, level);
}

void ledUpdate() {
  if (ledMode == LED_STATIC) return;
  unsigned long now = millis();
  if (now - lastLedMs < (ledMode == LED_BLINK ? 500 : 10)) return;
  lastLedMs = now;

  if (ledMode == LED_BREATHE) {
    // 0 → 255 → 0 삼각파 (약 5초 주기)
    uint16_t t = ledFrame % 510;
    analogWrite(LED_PIN, t < 255 ? t : 510 - t);
  } else {
    analogWrite(LED_PIN, (ledFrame & 1) ? 0 : ledLevel);
  }
  ledFrame++;
}

// ---------- 자동 테스트 ----------

void autoTest() {
  Serial.println(F("=== 자동 테스트 시작 ==="));

  Serial.println(F("[LED] 켜기 → 끄기 → 켜기"));
  ledSet(255); delay(700);
  ledSet(0);   delay(700);
  ledSet(255); delay(700);

  Serial.println(F("[LED] 밝기 0 → 255 서서히"));
  for (int v = 0; v <= 255; v += 5) { analogWrite(LED_PIN, v); delay(30); }
  ledSet(0);

  Serial.println(F("[모터] 정방향 1바퀴"));
  motorMove(STEPS_PER_REV);
  while (stepsRemaining != 0) motorUpdate();
  delay(500);

  Serial.println(F("[모터] 역방향 1바퀴"));
  motorMove(-STEPS_PER_REV);
  while (stepsRemaining != 0) motorUpdate();

  Serial.println(F("[동시] 모터 반 바퀴 왕복 + LED 숨쉬기"));
  ledMode = LED_BREATHE;
  ledFrame = 0;
  motorMove(STEPS_PER_REV / 2);
  while (stepsRemaining != 0) { motorUpdate(); ledUpdate(); }
  motorMove(-STEPS_PER_REV / 2);
  while (stepsRemaining != 0) { motorUpdate(); ledUpdate(); }
  ledSet(0);

  Serial.println(F("=== 자동 테스트 완료 ==="));
}

// ---------- 시리얼 명령 ----------

void printHelp() {
  Serial.println(F("--- 명령 목록 ---"));
  Serial.println(F(" t          : 자동 테스트 (LED + 모터)"));
  Serial.println(F(" n / o      : LED 켜기 / 끄기"));
  Serial.println(F(" L <0-255>  : LED 밝기 설정 후 켜기"));
  Serial.println(F(" a          : LED 숨쉬기 (서서히 밝아졌다 어두워짐)"));
  Serial.println(F(" k          : LED 깜빡이기"));
  Serial.println(F(" q          : 코일 배선 점검 (IN1~IN4 하나씩 1초)"));
  Serial.println(F(" m <스텝>   : 모터 이동 (음수 = 역방향), 예) m 512, m -512"));
  Serial.println(F(" R <바퀴>   : 모터 N바퀴 회전 (음수 = 역방향)"));
  Serial.println(F(" s <속도>   : 모터 속도 steps/sec"));
  Serial.println(F(" x          : 모터 정지 + 전원 해제"));
  Serial.println(F(" p          : 현재 상태 출력"));
  Serial.println(F(" h          : 도움말"));
}

void printStatus() {
  Serial.print(F("LED 핀=D")); Serial.print(LED_PIN);
  Serial.print(F(", 밝기="));   Serial.print(ledLevel);
  Serial.print(F(" | 모터 IN1~4=D")); Serial.print(IN1_PIN);
  Serial.print(F("~D"));        Serial.print(IN4_PIN);
  Serial.print(F(", 위치="));   Serial.print(position);
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
    case 'n': ledSet(ledLevel); break;
    case 'o': ledSet(0); break;
    case 'L':
      ledLevel = constrain(arg, 0, 255);
      ledSet(ledLevel);
      break;
    case 'a': ledMode = LED_BREATHE; ledFrame = 0; break;
    case 'k': ledMode = LED_BLINK; ledFrame = 0; break;
    case 'q': coilTest(); break;
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
  pinMode(LED_PIN, OUTPUT);
  ledSet(0);
  motorSetup();

  Serial.println(F("단색 LED 스트립 + 28BYJ-48 테스트"));
  printHelp();
}

void loop() {
  readSerial();
  motorUpdate();
  ledUpdate();
}
