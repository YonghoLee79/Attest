/*
 * 아두이노 2번 : LCD(I2C 16x2) + 도트 매트릭스(MAX7219 8x8) + 서보 모터
 *               + 초음파(HC-SR04) + 온습도(DHT11/22) + 조도(CDS) + RGB LED
 *
 * 라즈베리파이와 USB 시리얼(115200bps)로 통신한다.
 * 명령은 한 줄('\n' 종료) ASCII, 응답은 "OK ..." 또는 "ERR ..." 한 줄.
 *
 *   PING                 -> PONG ARD2
 *   LCD CLEAR            -> OK LCD CLEAR
 *   LCD PRINT row text   -> OK LCD PRINT       (row 0/1, 최대 16자)
 *   DOT CLEAR            -> OK DOT CLEAR
 *   DOT ALL              -> OK DOT ALL         (전체 점등)
 *   DOT ROW r hex        -> OK DOT ROW         (r 0-7, hex 00-FF)
 *   SERVO angle(0-180)   -> OK SERVO angle
 *   DIST                 -> OK DIST cm         (소수 1자리)
 *   DHT                  -> OK DHT temp hum
 *   LIGHT                -> OK LIGHT raw       (0-1023)
 *   RGB r g b            -> OK RGB
 *
 * 필요 라이브러리: LiquidCrystal I2C, LedControl, DHT sensor library(Adafruit), Servo
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LedControl.h>
#include <Servo.h>
#include <DHT.h>

// ---- 핀 / 하드웨어 설정 ----
const uint8_t LCD_ADDR = 0x27;               // 0x3F 인 모듈도 있음
const uint8_t DOT_DIN = 13, DOT_CLK = 10, DOT_CS = 11;
const uint8_t SERVO_PIN = 9;
const uint8_t US_TRIG = 8, US_ECHO = 7;
const uint8_t DHT_PIN = 2;
#define DHT_TYPE DHT11                        // DHT22 이면 변경
const uint8_t LIGHT_PIN = A0;
const uint8_t RGB_R = 3, RGB_G = 5, RGB_B = 6; // PWM 핀
const bool RGB_COMMON_ANODE = false;          // 공통 애노드형이면 true

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
LedControl dot(DOT_DIN, DOT_CLK, DOT_CS, 1);
Servo servo;
DHT dht(DHT_PIN, DHT_TYPE);

char line[64];
uint8_t lineLen = 0;

void writeRgb(int r, int g, int b) {
  if (RGB_COMMON_ANODE) { r = 255 - r; g = 255 - g; b = 255 - b; }
  analogWrite(RGB_R, r);
  analogWrite(RGB_G, g);
  analogWrite(RGB_B, b);
}

void handleLcd(char *args) {
  char *sub = strtok(args, " ");
  if (!sub) { Serial.println(F("ERR LCD ARGS")); return; }

  if (!strcmp(sub, "CLEAR")) {
    lcd.clear();
    Serial.println(F("OK LCD CLEAR"));
  } else if (!strcmp(sub, "PRINT")) {
    char *rowTok = strtok(NULL, " ");
    char *text = strtok(NULL, "");
    if (!rowTok) { Serial.println(F("ERR LCD ARGS")); return; }
    int row = atoi(rowTok);
    if (row < 0 || row > 1) { Serial.println(F("ERR LCD RANGE")); return; }
    char buf[17];
    snprintf(buf, sizeof(buf), "%-16s", text ? text : "");
    lcd.setCursor(0, row);
    lcd.print(buf);
    Serial.println(F("OK LCD PRINT"));
  } else {
    Serial.println(F("ERR LCD UNKNOWN"));
  }
}

void handleDot(char *args) {
  char *sub = strtok(args, " ");
  if (!sub) { Serial.println(F("ERR DOT ARGS")); return; }

  if (!strcmp(sub, "CLEAR")) {
    dot.clearDisplay(0);
    Serial.println(F("OK DOT CLEAR"));
  } else if (!strcmp(sub, "ALL")) {
    for (uint8_t r = 0; r < 8; r++) dot.setRow(0, r, 0xFF);
    Serial.println(F("OK DOT ALL"));
  } else if (!strcmp(sub, "ROW")) {
    char *rTok = strtok(NULL, " ");
    char *vTok = strtok(NULL, " ");
    if (!rTok || !vTok) { Serial.println(F("ERR DOT ARGS")); return; }
    int r = atoi(rTok);
    if (r < 0 || r > 7) { Serial.println(F("ERR DOT RANGE")); return; }
    dot.setRow(0, r, (byte)strtol(vTok, NULL, 16));
    Serial.println(F("OK DOT ROW"));
  } else {
    Serial.println(F("ERR DOT UNKNOWN"));
  }
}

void handleServo(char *args) {
  if (!args || !*args) { Serial.println(F("ERR SERVO ARGS")); return; }
  int a = atoi(args);
  if (a < 0 || a > 180) { Serial.println(F("ERR SERVO RANGE")); return; }
  servo.write(a);
  Serial.print(F("OK SERVO "));
  Serial.println(a);
}

void handleDist() {
  digitalWrite(US_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(US_TRIG, LOW);
  unsigned long us = pulseIn(US_ECHO, HIGH, 30000UL);  // 약 5m 까지
  if (us == 0) { Serial.println(F("ERR DIST TIMEOUT")); return; }
  Serial.print(F("OK DIST "));
  Serial.println(us / 58.0, 1);
}

void handleDht() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) { Serial.println(F("ERR DHT READ")); return; }
  Serial.print(F("OK DHT "));
  Serial.print(t, 1);
  Serial.print(' ');
  Serial.println(h, 1);
}

void handleRgb(char *args) {
  char *r = strtok(args, " ");
  char *g = strtok(NULL, " ");
  char *b = strtok(NULL, " ");
  if (!r || !g || !b) { Serial.println(F("ERR RGB ARGS")); return; }
  writeRgb(constrain(atoi(r), 0, 255), constrain(atoi(g), 0, 255), constrain(atoi(b), 0, 255));
  Serial.println(F("OK RGB"));
}

void handleLine(char *cmd) {
  char *head = strtok(cmd, " ");
  char *args = strtok(NULL, "");
  if (!head) return;
  if (!args) args = (char *)"";

  if (!strcmp(head, "PING")) Serial.println(F("PONG ARD2"));
  else if (!strcmp(head, "LCD")) handleLcd(args);
  else if (!strcmp(head, "DOT")) handleDot(args);
  else if (!strcmp(head, "SERVO")) handleServo(args);
  else if (!strcmp(head, "DIST")) handleDist();
  else if (!strcmp(head, "DHT")) handleDht();
  else if (!strcmp(head, "LIGHT")) { Serial.print(F("OK LIGHT ")); Serial.println(analogRead(LIGHT_PIN)); }
  else if (!strcmp(head, "RGB")) handleRgb(args);
  else Serial.println(F("ERR UNKNOWN"));
}

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  dot.shutdown(0, false);
  dot.setIntensity(0, 4);
  dot.clearDisplay(0);
  servo.attach(SERVO_PIN);
  servo.write(90);
  pinMode(US_TRIG, OUTPUT);
  pinMode(US_ECHO, INPUT);
  dht.begin();
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);
  writeRgb(0, 0, 0);
  Serial.println(F("READY ARD2"));
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
