/*
 * 아두이노 1번 : LED 스트립(WS2812B) + 스테핑 모터(28BYJ-48 + ULN2003)
 *
 * 라즈베리파이와 USB 시리얼(115200bps)로 통신한다.
 * 명령은 한 줄('\n' 종료) ASCII, 응답은 "OK ..." 또는 "ERR ..." 한 줄.
 *
 *   PING                 -> PONG ARD1
 *   LED FILL r g b       -> OK LED FILL
 *   LED PIX i r g b      -> OK LED PIX
 *   LED BRIGHT n(0-255)  -> OK LED BRIGHT
 *   LED CLEAR            -> OK LED CLEAR
 *   STEP SPEED rpm(1-15) -> OK STEP SPEED
 *   STEP MOVE n(+/-)     -> OK STEP MOVE n   (이동이 끝난 뒤 응답)
 *   STEP POS             -> OK STEP POS p
 *   STEP RELEASE         -> OK STEP RELEASE  (코일 전류 차단)
 *
 * 필요 라이브러리: Adafruit NeoPixel
 */
#include <Adafruit_NeoPixel.h>
#include <Stepper.h>

// ---- 핀 / 하드웨어 설정 ----
const uint8_t LED_PIN = 6;
const uint16_t LED_COUNT = 30;          // 스트립의 LED 개수에 맞게 수정
const int STEPS_PER_REV = 2048;         // 28BYJ-48 (풀스텝 기준)
const uint8_t STEP_IN1 = 8, STEP_IN2 = 9, STEP_IN3 = 10, STEP_IN4 = 11;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
// 28BYJ-48 은 IN1, IN3, IN2, IN4 순서로 넘겨야 올바르게 회전한다.
Stepper stepper(STEPS_PER_REV, STEP_IN1, STEP_IN3, STEP_IN2, STEP_IN4);

long stepPos = 0;
char line[64];
uint8_t lineLen = 0;

void releaseStepper() {
  digitalWrite(STEP_IN1, LOW);
  digitalWrite(STEP_IN2, LOW);
  digitalWrite(STEP_IN3, LOW);
  digitalWrite(STEP_IN4, LOW);
}

bool parseRgb(char *p, long v[], uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    char *tok = strtok(i == 0 ? p : NULL, " ");
    if (!tok) return false;
    v[i] = atol(tok);
  }
  return true;
}

void handleLed(char *args) {
  char *sub = strtok(args, " ");
  char *rest = strtok(NULL, "");
  if (!sub) { Serial.println(F("ERR LED ARGS")); return; }
  long v[4];

  if (!strcmp(sub, "FILL")) {
    if (!rest || !parseRgb(rest, v, 3)) { Serial.println(F("ERR LED ARGS")); return; }
    strip.fill(strip.Color(constrain(v[0], 0, 255), constrain(v[1], 0, 255), constrain(v[2], 0, 255)));
    strip.show();
    Serial.println(F("OK LED FILL"));
  } else if (!strcmp(sub, "PIX")) {
    if (!rest || !parseRgb(rest, v, 4)) { Serial.println(F("ERR LED ARGS")); return; }
    if (v[0] < 0 || v[0] >= LED_COUNT) { Serial.println(F("ERR LED RANGE")); return; }
    strip.setPixelColor(v[0], strip.Color(constrain(v[1], 0, 255), constrain(v[2], 0, 255), constrain(v[3], 0, 255)));
    strip.show();
    Serial.println(F("OK LED PIX"));
  } else if (!strcmp(sub, "BRIGHT")) {
    if (!rest) { Serial.println(F("ERR LED ARGS")); return; }
    strip.setBrightness(constrain(atol(rest), 0, 255));
    strip.show();
    Serial.println(F("OK LED BRIGHT"));
  } else if (!strcmp(sub, "CLEAR")) {
    strip.clear();
    strip.show();
    Serial.println(F("OK LED CLEAR"));
  } else {
    Serial.println(F("ERR LED UNKNOWN"));
  }
}

void handleStep(char *args) {
  char *sub = strtok(args, " ");
  char *rest = strtok(NULL, "");
  if (!sub) { Serial.println(F("ERR STEP ARGS")); return; }

  if (!strcmp(sub, "SPEED")) {
    if (!rest) { Serial.println(F("ERR STEP ARGS")); return; }
    long rpm = atol(rest);
    if (rpm < 1 || rpm > 15) { Serial.println(F("ERR STEP RANGE")); return; }
    stepper.setSpeed(rpm);
    Serial.println(F("OK STEP SPEED"));
  } else if (!strcmp(sub, "MOVE")) {
    if (!rest) { Serial.println(F("ERR STEP ARGS")); return; }
    long n = atol(rest);
    if (n < -STEPS_PER_REV * 4L || n > STEPS_PER_REV * 4L) { Serial.println(F("ERR STEP RANGE")); return; }
    stepper.step(n);  // 블로킹: 이동이 끝날 때까지 기다린다
    stepPos += n;
    releaseStepper();
    Serial.print(F("OK STEP MOVE "));
    Serial.println(n);
  } else if (!strcmp(sub, "POS")) {
    Serial.print(F("OK STEP POS "));
    Serial.println(stepPos);
  } else if (!strcmp(sub, "RELEASE")) {
    releaseStepper();
    Serial.println(F("OK STEP RELEASE"));
  } else {
    Serial.println(F("ERR STEP UNKNOWN"));
  }
}

void handleLine(char *cmd) {
  char *head = strtok(cmd, " ");
  char *args = strtok(NULL, "");
  if (!head) return;

  if (!strcmp(head, "PING")) Serial.println(F("PONG ARD1"));
  else if (!strcmp(head, "LED")) handleLed(args ? args : (char *)"");
  else if (!strcmp(head, "STEP")) handleStep(args ? args : (char *)"");
  else Serial.println(F("ERR UNKNOWN"));
}

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.setBrightness(64);
  strip.clear();
  strip.show();
  stepper.setSpeed(12);
  pinMode(STEP_IN1, OUTPUT);
  pinMode(STEP_IN2, OUTPUT);
  pinMode(STEP_IN3, OUTPUT);
  pinMode(STEP_IN4, OUTPUT);
  releaseStepper();
  Serial.println(F("READY ARD1"));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = '\0';
      handleLine(line);
      lineLen = 0;
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}
