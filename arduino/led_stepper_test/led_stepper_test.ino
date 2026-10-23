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
#define LED_BRIGHTNESS 1     // 0~255 시작 밝기 (n 으로 켤 때, 전원 켤 때)

// 전원 켜면 바로 불꽃 LED + 모터 연속 회전 시작 (0 = 명령 올 때까지 대기)
#define AUTO_RUN       1

// --- 28BYJ-48 + ULN2003 ---
// MICROSTEPS: 풀스텝 1개를 몇 개로 나눌지 (2, 4, 8, 16). 0 = 기존 하프스텝 방식
// 마이크로스텝은 PWM 으로 코일 전류를 조절하므로 IN1~IN4 가
// 반드시 D9, D10, D11, D3 이어야 함 (31kHz PWM 으로 바꿔서 소음 없음)
#define MICROSTEPS     16

#if MICROSTEPS > 0
  #define IN1_PIN        9
  #define IN2_PIN        10
  #define IN3_PIN        11
  #define IN4_PIN        3
  #define STEPS_PER_REV  (2048L * MICROSTEPS)
  #define DEFAULT_RPM    1.0                // 천천히: 1분에 1바퀴
  #define DEFAULT_SPEED  ((unsigned int)(STEPS_PER_REV * DEFAULT_RPM / 60))
#else
  #define IN1_PIN        2
  #define IN2_PIN        3
  #define IN3_PIN        4
  #define IN4_PIN        5
  #define STEPS_PER_REV  4096L  // 하프스텝 기준
  #define DEFAULT_SPEED  400    // steps/sec (~800 이상이면 탈조 가능)
#endif

// ================================================

// ---------- 스텝 모터 (논블로킹) ----------

long stepsRemaining = 0;     // 남은 스텝 수 (부호 = 방향)
long position = 0;           // 현재 위치 (스텝)
unsigned int stepSpeed = DEFAULT_SPEED;
unsigned long lastStepUs = 0;
bool motorContinuous = false;  // true 면 'x' 할 때까지 계속 회전
bool motorForward = true;

const uint8_t coilPins[4] = {IN1_PIN, IN2_PIN, IN3_PIN, IN4_PIN};

#if MICROSTEPS > 0
// 전기각 1주기(풀스텝 4개)를 4*MICROSTEPS 칸으로 나눈 사인 테이블.
// IN1=A, IN2=B, IN3=A', IN4=B' 순서라 A=cos, B=sin 의 양/음 반쪽을 각 코일에 줌
#define SEQ_LEN (4 * MICROSTEPS)
uint8_t sineTable[SEQ_LEN];
uint8_t seqIndex = 0;

void applyPhase() {
  uint8_t a  = sineTable[(seqIndex + MICROSTEPS) % SEQ_LEN];  // cos
  uint8_t b  = sineTable[seqIndex];                            // sin
  uint8_t na = sineTable[(seqIndex + 3 * MICROSTEPS) % SEQ_LEN];  // -cos
  uint8_t nb = sineTable[(seqIndex + 2 * MICROSTEPS) % SEQ_LEN];  // -sin
  analogWrite(IN1_PIN, a);
  analogWrite(IN2_PIN, b);
  analogWrite(IN3_PIN, na);
  analogWrite(IN4_PIN, nb);
}
#else
#define SEQ_LEN 8
const uint8_t halfStepSeq[8][4] = {
  {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
  {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1},
};
uint8_t seqIndex = 0;

void applyPhase() {
  for (uint8_t i = 0; i < 4; i++) digitalWrite(coilPins[i], halfStepSeq[seqIndex][i]);
}
#endif

void motorRelease() {
  // 코일 전류 차단 (발열 방지)
  for (uint8_t i = 0; i < 4; i++) digitalWrite(coilPins[i], LOW);
}

void motorSetup() {
  for (uint8_t i = 0; i < 4; i++) pinMode(coilPins[i], OUTPUT);
#if MICROSTEPS > 0
  // 사인 양의 반쪽만 사용 (음수는 0) → 반대편 코일이 나머지 반쪽 담당
  for (uint8_t i = 0; i < SEQ_LEN; i++) {
    float s = sin(2.0 * PI * i / SEQ_LEN);
    sineTable[i] = s > 0 ? (uint8_t)(s * 255 + 0.5) : 0;
  }
  // D9/D10(Timer1), D11/D3(Timer2) PWM 을 490Hz → 31kHz 로 (윙 소리 제거).
  // Timer0(D5, D6, millis)은 건드리지 않음
  TCCR1B = (TCCR1B & 0xF8) | 0x01;
  TCCR2B = (TCCR2B & 0xF8) | 0x01;
#endif
  motorRelease();
}

void motorStepOnce(bool forward) {
  seqIndex = (seqIndex + (forward ? 1 : SEQ_LEN - 1)) % SEQ_LEN;
  applyPhase();
  position += forward ? 1 : -1;
}

void motorMove(long steps) {
  motorContinuous = false;
  if (steps == 0) return;
  stepsRemaining = steps;
  lastStepUs = micros();
}

void motorRun(bool forward) {
  stepsRemaining = 0;
  motorContinuous = true;
  motorForward = forward;
  lastStepUs = micros();
}

void motorStop() {
  motorContinuous = false;
  stepsRemaining = 0;
  motorRelease();
}

void motorUpdate() {
  if (!motorContinuous && stepsRemaining == 0) return;
  unsigned long now = micros();
  if (now - lastStepUs < 1000000UL / stepSpeed) return;
  lastStepUs = now;

  if (motorContinuous) {
    motorStepOnce(motorForward);
    return;
  }

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
  motorContinuous = false;
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

enum LedMode { LED_STATIC, LED_BREATHE, LED_BLINK, LED_FLAME };
LedMode ledMode = LED_STATIC;
uint8_t ledLevel = LED_BRIGHTNESS;   // 켤 때 밝기
uint16_t ledFrame = 0;
unsigned long lastLedMs = 0;

// 숨쉬기: 아래 밝기를 올라갔다 내려오며 순서대로 표시
const uint8_t breatheLevels[] = {1, 50, 100, 150, 200, 250};
#define BREATHE_COUNT (sizeof(breatheLevels) / sizeof(breatheLevels[0]))
unsigned int breatheHoldMs = 1000;   // 단계마다 유지 시간

// 불꽃: 무작위 목표 밝기로 부드럽게 따라가며 가끔 크게 일렁임
uint8_t flamePeak = 255;      // 불꽃 최대 밝기
int flameLevel = 0;           // 현재 밝기 (0~255, peak 적용 전)
int flameTarget = 180;
unsigned long nextFlameTargetMs = 0;

void flameUpdate(unsigned long now) {
  if (now >= nextFlameTargetMs) {
    if (random(12) == 0) {
      flameTarget = random(40, 110);    // 가끔 크게 사그라듦
    } else {
      flameTarget = random(150, 256);   // 평소 일렁임
    }
    nextFlameTargetMs = now + random(40, 150);
  }
  // 목표 쪽으로 30% 씩 이동 → 깜빡임 없이 출렁임
  flameLevel += (flameTarget - flameLevel) * 3 / 10;
  analogWrite(LED_PIN, (uint16_t)flameLevel * flamePeak / 255);
}

void ledSet(uint8_t level) {
  ledMode = LED_STATIC;
  analogWrite(LED_PIN, level);
}

void ledUpdate() {
  if (ledMode == LED_STATIC) return;
  unsigned long now = millis();
  unsigned int interval = ledMode == LED_BLINK ? 500 : ledMode == LED_FLAME ? 15 : breatheHoldMs;
  if (ledFrame > 0 && now - lastLedMs < interval) return;
  lastLedMs = now;

  if (ledMode == LED_FLAME) {
    flameUpdate(now);
  } else if (ledMode == LED_BREATHE) {
    // 1 → 50 → ... → 250 → ... → 50 → 1 반복
    uint8_t period = 2 * (BREATHE_COUNT - 1);
    uint8_t i = ledFrame % period;
    if (i >= BREATHE_COUNT) i = period - i;
    analogWrite(LED_PIN, breatheLevels[i]);
    Serial.print(F("[LED] 밝기 "));
    Serial.println(breatheLevels[i]);
  } else {
    analogWrite(LED_PIN, (ledFrame & 1) ? 0 : ledLevel);
  }
  ledFrame++;
}

// ---------- 자동 테스트 ----------

void autoTest() {
  Serial.println(F("=== 자동 테스트 시작 ==="));
  unsigned int savedSpeed = stepSpeed;
  stepSpeed = 250 * (MICROSTEPS > 0 ? MICROSTEPS : 2);   // 테스트는 빠르게 (~7 RPM)

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
  breatheHoldMs = 300;
  motorMove(STEPS_PER_REV / 2);
  while (stepsRemaining != 0) { motorUpdate(); ledUpdate(); }
  motorMove(-STEPS_PER_REV / 2);
  while (stepsRemaining != 0) { motorUpdate(); ledUpdate(); }
  ledSet(0);
  breatheHoldMs = 1000;

  stepSpeed = savedSpeed;
  Serial.println(F("=== 자동 테스트 완료 ==="));
}

// ---------- 시리얼 명령 ----------

void printHelp() {
  Serial.println(F("--- 명령 목록 ---"));
  Serial.println(F(" t          : 자동 테스트 (LED + 모터)"));
  Serial.println(F(" n / o      : LED 켜기 / 끄기"));
  Serial.println(F(" L <0-255>  : LED 밝기 설정 후 켜기"));
  Serial.println(F(" a [ms]     : LED 숨쉬기 1→50→100→150→200→250→...→1 (단계 유지시간, 기본 1000)"));
  Serial.println(F(" k          : LED 깜빡이기"));
  Serial.println(F(" f [0-255]  : LED 불꽃 일렁임 (최대 밝기, 기본 255)"));
  Serial.println(F(" q          : 코일 배선 점검 (IN1~IN4 하나씩 1초)"));
  Serial.println(F(" m <스텝>   : 모터 이동 (음수 = 역방향), 예) m 512, m -512"));
  Serial.println(F(" R <바퀴>   : 모터 N바퀴 회전 (음수 = 역방향)"));
  Serial.println(F(" g / G      : 모터 계속 회전 정방향 / 역방향 ('x' 로 정지)"));
  Serial.println(F(" v <RPM>    : 모터 속도 RPM (소수 가능, 예: v 0.5, v 3)"));
  Serial.println(F(" s <속도>   : 모터 속도 steps/sec"));
  Serial.println(F(" x          : 모터 정지 + 전원 해제"));
  Serial.println(F(" p          : 현재 상태 출력"));
  Serial.println(F(" h          : 도움말"));
}

void printStatus() {
  Serial.print(F("LED 핀=D")); Serial.print(LED_PIN);
  Serial.print(F(", 밝기="));   Serial.print(ledLevel);
  Serial.print(F(" | 모터 IN1~4=D"));
  for (uint8_t i = 0; i < 4; i++) {
    if (i) Serial.print(F(",D"));
    Serial.print(coilPins[i]);
  }
  Serial.print(F(", 마이크로스텝=")); Serial.print(MICROSTEPS);
  Serial.print(F(", 위치="));   Serial.print(position);
  Serial.print(F(", 남은 스텝=")); Serial.print(stepsRemaining);
  Serial.print(F(", 속도="));   Serial.print(stepSpeed);
  Serial.print(F(" steps/s ("));
  Serial.print(stepSpeed * 60.0 / STEPS_PER_REV, 2);
  Serial.print(F(" RPM)"));
  Serial.print(F(", 1바퀴=")); Serial.print(STEPS_PER_REV);
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
    case 'a':
      if (arg > 0) breatheHoldMs = arg;
      ledMode = LED_BREATHE;
      ledFrame = 0;
      break;
    case 'k': ledMode = LED_BLINK; ledFrame = 0; break;
    case 'f':
      if (arg > 0) flamePeak = constrain(arg, 1, 255);
      ledMode = LED_FLAME;
      ledFrame = 0;
      break;
    case 'g': motorRun(true); break;
    case 'G': motorRun(false); break;
    case 'q': coilTest(); break;
    case 'm': motorMove(arg); break;
    case 'R': motorMove(arg * STEPS_PER_REV); break;
    case 's':
      if (arg > 0) stepSpeed = arg;
      break;
    case 'v': {
      // RPM 으로 속도 지정 (소수 가능: v 0.5)
      float rpm = atof(line + 1);
      if (rpm > 0) stepSpeed = max(1L, (long)(rpm * STEPS_PER_REV / 60));
      break;
    }
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

  randomSeed(analogRead(A0));
#if AUTO_RUN
  // 밝기 1 은 PWM 최저 단계라 불꽃처럼 일렁일 여지가 없어 고정 점등 (불꽃: f)
  ledSet(ledLevel);
  motorRun(true);
  Serial.println(F("[자동] LED 밝기 1 + 모터 연속 회전 시작 (정지: x, LED 끄기: o, 불꽃: f)"));
#endif
}

void loop() {
  readSerial();
  motorUpdate();
  ledUpdate();
}
