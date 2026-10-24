"""하드웨어 없이 테스트 러너를 검증하기 위한 가상 아두이노.

펌웨어(.ino)와 같은 명령/응답 규칙을 따른다. 고장 주입 옵션으로
FAIL 판정 경로도 확인할 수 있다.
"""
import queue
import random
import threading
import time


class _SimTransport:
    """pyserial.Serial 처럼 동작하는 가상 포트. 명령은 별도 스레드에서 처리된다."""

    def __init__(self, boot_msg, time_scale=1.0):
        self._out = queue.Queue()
        self._in = queue.Queue()
        self.time_scale = time_scale
        self._out.put((boot_msg + "\n").encode())
        threading.Thread(target=self._worker, daemon=True).start()

    def write(self, data):
        for line in data.decode().splitlines():
            self._in.put(line)

    def readline(self):
        try:
            return self._out.get(timeout=0.05)
        except queue.Empty:
            return b""

    def reset_input_buffer(self):
        while not self._out.empty():
            self._out.get_nowait()

    def close(self):
        self._in.put(None)

    def sleep(self, sec):
        time.sleep(sec * self.time_scale)

    def _worker(self):
        while True:
            line = self._in.get()
            if line is None:
                return
            try:
                resp = self.handle(line.split())
            except (ValueError, IndexError):
                resp = "ERR ARGS"
            if resp is not None:
                self._out.put((resp + "\n").encode())

    def handle(self, tok):
        raise NotImplementedError


class SimArduino1(_SimTransport):
    def __init__(self, led_count=30, time_scale=0.05, mute=False):
        self.led = [(0, 0, 0)] * led_count
        self.brightness = 64
        self.rpm = 12
        self.pos = 0
        self.mute = mute  # True 면 응답하지 않음(케이블 빠짐/펌웨어 멈춤 흉내)
        super().__init__("READY ARD1", time_scale)

    def handle(self, tok):
        if self.mute:
            return None
        if not tok:
            return "ERR UNKNOWN"
        if tok[0] == "PING":
            return "PONG ARD1"
        if tok[0] == "LED":
            sub = tok[1]
            if sub == "FILL":
                self.led = [tuple(int(x) for x in tok[2:5])] * len(self.led)
                return "OK LED FILL"
            if sub == "PIX":
                i = int(tok[2])
                if not 0 <= i < len(self.led):
                    return "ERR LED RANGE"
                self.led[i] = tuple(int(x) for x in tok[3:6])
                return "OK LED PIX"
            if sub == "BRIGHT":
                self.brightness = int(tok[2])
                return "OK LED BRIGHT"
            if sub == "CLEAR":
                self.led = [(0, 0, 0)] * len(self.led)
                return "OK LED CLEAR"
            return "ERR LED UNKNOWN"
        if tok[0] == "STEP":
            sub = tok[1]
            if sub == "SPEED":
                rpm = int(tok[2])
                if not 1 <= rpm <= 15:
                    return "ERR STEP RANGE"
                self.rpm = rpm
                return "OK STEP SPEED"
            if sub == "MOVE":
                n = int(tok[2])
                self.sleep(abs(n) / 2048 * 60 / self.rpm)
                self.pos += n
                return f"OK STEP MOVE {n}"
            if sub == "POS":
                return f"OK STEP POS {self.pos}"
            if sub == "RELEASE":
                return "OK STEP RELEASE"
            return "ERR STEP UNKNOWN"
        return "ERR UNKNOWN"


class SimArduino2(_SimTransport):
    def __init__(self, time_scale=0.05, distance=42.0, temp=24.5, hum=48.0,
                 light=512, dht_fail=False, ultrasonic_fail=False):
        self.lcd = ["", ""]
        self.dot = [0] * 8
        self.servo = 90
        self.rgb = (0, 0, 0)
        self.distance = distance
        self.temp, self.hum = temp, hum
        self.light = light
        self.dht_fail = dht_fail
        self.ultrasonic_fail = ultrasonic_fail
        super().__init__("READY ARD2", time_scale)

    def handle(self, tok):
        if not tok:
            return "ERR UNKNOWN"
        cmd = tok[0]
        if cmd == "PING":
            return "PONG ARD2"
        if cmd == "LCD":
            if tok[1] == "CLEAR":
                self.lcd = ["", ""]
                return "OK LCD CLEAR"
            if tok[1] == "PRINT":
                row = int(tok[2])
                if row not in (0, 1):
                    return "ERR LCD RANGE"
                self.lcd[row] = " ".join(tok[3:])[:16]
                return "OK LCD PRINT"
            return "ERR LCD UNKNOWN"
        if cmd == "DOT":
            if tok[1] == "CLEAR":
                self.dot = [0] * 8
                return "OK DOT CLEAR"
            if tok[1] == "ALL":
                self.dot = [0xFF] * 8
                return "OK DOT ALL"
            if tok[1] == "ROW":
                r = int(tok[2])
                if not 0 <= r <= 7:
                    return "ERR DOT RANGE"
                self.dot[r] = int(tok[3], 16)
                return "OK DOT ROW"
            return "ERR DOT UNKNOWN"
        if cmd == "SERVO":
            a = int(tok[1])
            if not 0 <= a <= 180:
                return "ERR SERVO RANGE"
            self.servo = a
            return f"OK SERVO {a}"
        if cmd == "DIST":
            if self.ultrasonic_fail:
                return "ERR DIST TIMEOUT"
            return f"OK DIST {self.distance + random.uniform(-0.5, 0.5):.1f}"
        if cmd == "DHT":
            self.sleep(0.25)
            if self.dht_fail:
                return "ERR DHT READ"
            return f"OK DHT {self.temp:.1f} {self.hum:.1f}"
        if cmd == "LIGHT":
            return f"OK LIGHT {max(0, min(1023, self.light + random.randint(-3, 3)))}"
        if cmd == "RGB":
            self.rgb = tuple(int(x) for x in tok[1:4])
            return "OK RGB"
        return "ERR UNKNOWN"
