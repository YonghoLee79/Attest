"""아두이노와의 한 줄 명령/응답 시리얼 링크."""
import threading
import time


class DeviceError(Exception):
    """아두이노가 ERR 응답을 돌려준 경우."""


class DeviceTimeout(Exception):
    """제한 시간 안에 응답이 오지 않은 경우."""


class ArduinoLink:
    """transport 는 pyserial.Serial 과 같은 write()/readline()/reset_input_buffer()/close() 를 가진 객체.

    readline() 은 짧은 타임아웃(예: 0.1초) 후 b'' 를 돌려줘야 한다.
    """

    def __init__(self, transport, name="?", default_timeout=2.0):
        self.transport = transport
        self.name = name
        self.default_timeout = default_timeout
        self._lock = threading.Lock()

    def readline(self, timeout):
        deadline = time.monotonic() + timeout
        buf = b""
        while time.monotonic() < deadline:
            chunk = self.transport.readline()
            if not chunk:
                continue
            buf += chunk
            if buf.endswith(b"\n"):
                return buf.decode("ascii", errors="replace").strip()
        return None

    def wait_for(self, prefix, timeout):
        """부팅 메시지(READY ...) 등 특정 접두어로 시작하는 줄을 기다린다."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            got = self.readline(remaining)
            if got and got.startswith(prefix):
                return got

    def command(self, cmd, timeout=None):
        """명령을 보내고 OK/PONG 응답의 나머지 토큰을 돌려준다."""
        timeout = self.default_timeout if timeout is None else timeout
        with self._lock:
            self.transport.reset_input_buffer()
            self.transport.write((cmd + "\n").encode("ascii"))
            deadline = time.monotonic() + timeout
            while True:
                remaining = deadline - time.monotonic()
                resp = self.readline(remaining) if remaining > 0 else None
                if resp is None:
                    raise DeviceTimeout(f"{self.name}: '{cmd}' 응답 없음 ({timeout:.1f}s)")
                if resp.startswith("ERR"):
                    raise DeviceError(f"{self.name}: '{cmd}' -> {resp}")
                if resp.startswith("OK") or resp.startswith("PONG"):
                    return resp.split()[1:]
                # READY 같은 비동기 메시지는 무시하고 계속 기다린다.

    def close(self):
        self.transport.close()


class Arduino1:
    """LED 스트립 + 스테핑 모터."""

    def __init__(self, link):
        self.link = link

    def ping(self):
        return self.link.command("PING")

    def led_fill(self, r, g, b):
        self.link.command(f"LED FILL {r} {g} {b}")

    def led_pixel(self, i, r, g, b):
        self.link.command(f"LED PIX {i} {r} {g} {b}")

    def led_brightness(self, n):
        self.link.command(f"LED BRIGHT {n}")

    def led_clear(self):
        self.link.command("LED CLEAR")

    def step_speed(self, rpm):
        self.link.command(f"STEP SPEED {rpm}")

    def step_move(self, steps, rpm):
        # 블로킹 이동이므로 예상 시간 + 여유를 타임아웃으로 준다.
        expected = abs(steps) / 2048 * 60 / rpm
        self.link.command(f"STEP MOVE {steps}", timeout=expected * 1.5 + 2)
        return expected

    def step_pos(self):
        return int(self.link.command("STEP POS")[-1])

    def step_release(self):
        self.link.command("STEP RELEASE")


class Arduino2:
    """LCD + 도트 매트릭스 + 서보 + 초음파 + 온습도 + 조도 + RGB."""

    def __init__(self, link):
        self.link = link

    def ping(self):
        return self.link.command("PING")

    def lcd_clear(self):
        self.link.command("LCD CLEAR")

    def lcd_print(self, row, text):
        self.link.command(f"LCD PRINT {row} {text[:16]}")

    def dot_clear(self):
        self.link.command("DOT CLEAR")

    def dot_all(self):
        self.link.command("DOT ALL")

    def dot_pattern(self, rows):
        for r, v in enumerate(rows):
            self.link.command(f"DOT ROW {r} {v:02X}")

    def servo(self, angle):
        self.link.command(f"SERVO {angle}")

    def distance_cm(self):
        return float(self.link.command("DIST")[1])

    def dht(self):
        resp = self.link.command("DHT", timeout=3)
        return float(resp[1]), float(resp[2])

    def light(self):
        return int(self.link.command("LIGHT")[1])

    def rgb(self, r, g, b):
        self.link.command(f"RGB {r} {g} {b}")
