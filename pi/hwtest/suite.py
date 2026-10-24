"""통합 테스트 케이스 모음.

각 테스트는 Context 를 받아 실행되며, 실패 시 예외를 던진다.
- AssertionError / DeviceError / DeviceTimeout -> FAIL
- Warn                                          -> WARN (동작은 하지만 의심스러움)
- Skip                                          -> SKIP (해당 보드 미연결 등)
"""
import statistics
import threading
import time
from dataclasses import dataclass, field

from .link import DeviceError, DeviceTimeout


class Skip(Exception):
    pass


class Warn(Exception):
    pass


@dataclass
class Context:
    ard1: object = None            # link.Arduino1
    ard2: object = None            # link.Arduino2
    interactive: bool = False      # 사람이 눈으로 확인하는 질문을 할지
    timing_scale: float = 1.0      # 시뮬레이터는 시간을 압축하므로 < 1
    notes: list = field(default_factory=list)

    def need(self, *names):
        for n in names:
            if getattr(self, n) is None:
                raise Skip(f"{n.upper()} 미연결")

    def confirm(self, question):
        """육안 확인. 비대화식 모드면 None(판정 보류)."""
        if not self.interactive:
            return None
        ans = input(f"    ? {question} [y/n] ").strip().lower()
        assert ans.startswith("y"), f"육안 확인 실패: {question}"
        return True

    def note(self, msg):
        self.notes.append(msg)


TESTS = []


def test(group, name):
    def deco(fn):
        TESTS.append((group, name, fn))
        return fn
    return deco


# --------------------------------------------------------------------------
# 아두이노 1번 : LED 스트립 + 스테핑 모터
# --------------------------------------------------------------------------
@test("ARD1", "통신(PING)")
def t_ard1_ping(ctx):
    ctx.need("ard1")
    assert ctx.ard1.ping() == ["ARD1"]


@test("ARD1", "LED 스트립 색상")
def t_led_colors(ctx):
    ctx.need("ard1")
    a = ctx.ard1
    a.led_brightness(64)
    for rgb in [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]:
        a.led_fill(*rgb)
        time.sleep(0.4 * ctx.timing_scale)
    ctx.confirm("LED 스트립이 빨강→초록→파랑→흰색으로 바뀌었나요?")
    a.led_clear()


@test("ARD1", "LED 스트립 개별 픽셀/밝기")
def t_led_pixels(ctx):
    ctx.need("ard1")
    a = ctx.ard1
    a.led_clear()
    for i in range(10):
        a.led_pixel(i, 0, 80, 255)
        time.sleep(0.05 * ctx.timing_scale)
    for b in (16, 64, 160, 255, 64):
        a.led_brightness(b)
        time.sleep(0.15 * ctx.timing_scale)
    ctx.confirm("앞쪽 10개 LED 가 하나씩 켜지고 밝기가 변했나요?")
    try:
        a.led_pixel(9999, 1, 1, 1)
    except DeviceError:
        pass
    else:
        raise AssertionError("범위 밖 픽셀 인덱스가 거부되지 않음")
    a.led_clear()


@test("ARD1", "스테핑 모터 정/역회전")
def t_stepper(ctx):
    ctx.need("ard1")
    a = ctx.ard1
    rpm = 12
    a.step_speed(rpm)
    start = a.step_pos()
    for steps in (512, -512):  # 1/4 회전 정방향, 역방향
        t0 = time.monotonic()
        expected = a.step_move(steps, rpm) * ctx.timing_scale
        elapsed = time.monotonic() - t0
        # 즉시 응답이 오면 모터 루틴이 실행되지 않은 것
        assert elapsed >= expected * 0.6, f"이동이 너무 빠름 ({elapsed:.2f}s < 예상 {expected:.2f}s)"
    assert a.step_pos() == start, f"원점 복귀 실패: {start} -> {a.step_pos()}"
    a.step_release()
    ctx.confirm("모터가 1/4 바퀴 돌았다가 제자리로 돌아왔나요?")


# --------------------------------------------------------------------------
# 아두이노 2번 : LCD / 도트 / 서보 / 초음파 / 온습도 / 조도 / RGB
# --------------------------------------------------------------------------
@test("ARD2", "통신(PING)")
def t_ard2_ping(ctx):
    ctx.need("ard2")
    assert ctx.ard2.ping() == ["ARD2"]


@test("ARD2", "LCD 출력")
def t_lcd(ctx):
    ctx.need("ard2")
    b = ctx.ard2
    b.lcd_clear()
    b.lcd_print(0, "INTEGRATION")
    b.lcd_print(1, "TEST 0123456789")
    ctx.confirm("LCD 에 'INTEGRATION / TEST 0123456789' 가 보이나요?")


@test("ARD2", "도트 매트릭스")
def t_dot(ctx):
    ctx.need("ard2")
    b = ctx.ard2
    b.dot_all()
    time.sleep(0.5 * ctx.timing_scale)
    b.dot_pattern([0x00, 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18])  # 하트
    ctx.confirm("도트 매트릭스가 전체 점등 후 하트 모양이 되었나요?")
    time.sleep(0.5 * ctx.timing_scale)
    b.dot_clear()


@test("ARD2", "서보 모터 스윕")
def t_servo(ctx):
    ctx.need("ard2")
    b = ctx.ard2
    for angle in (0, 90, 180, 90):
        b.servo(angle)
        time.sleep(0.6 * ctx.timing_scale)
    try:
        b.servo(200)
    except DeviceError:
        pass
    else:
        raise AssertionError("범위 밖 각도(200)가 거부되지 않음")
    ctx.confirm("서보가 0° → 90° → 180° → 90° 로 움직였나요?")


@test("ARD2", "초음파 거리 센서")
def t_ultrasonic(ctx):
    ctx.need("ard2")
    vals, errors = [], 0
    for _ in range(7):
        try:
            vals.append(ctx.ard2.distance_cm())
        except DeviceError:
            errors += 1
        time.sleep(0.06)
    assert len(vals) >= 4, f"유효 측정 {len(vals)}/7 (에코 없음 {errors}회) - TRIG/ECHO 배선 확인"
    med = statistics.median(vals)
    assert 2 <= med <= 400, f"측정 범위 밖: {med:.1f}cm"
    spread = max(vals) - min(vals)
    ctx.note(f"거리 중앙값 {med:.1f}cm, 편차 {spread:.1f}cm")
    if spread > 20:
        raise Warn(f"측정값 흔들림이 큼 ({spread:.1f}cm) - 센서 앞 장애물 고정 후 재확인")


@test("ARD2", "온습도 센서(DHT)")
def t_dht(ctx):
    ctx.need("ard2")
    last_err = None
    for _ in range(3):  # DHT 는 간헐적으로 읽기 실패가 난다
        try:
            t, h = ctx.ard2.dht()
            break
        except DeviceError as e:
            last_err = e
            time.sleep(2.0 * ctx.timing_scale)  # DHT11 최소 읽기 간격
    else:
        raise AssertionError(f"3회 연속 읽기 실패: {last_err} - DATA 핀/풀업 저항 확인")
    assert -10 <= t <= 60, f"온도 비정상 {t}°C"
    assert 0 <= h <= 100, f"습도 비정상 {h}%"
    ctx.note(f"온도 {t:.1f}°C, 습도 {h:.1f}%")


@test("ARD2", "조도 센서")
def t_light(ctx):
    ctx.need("ard2")
    vals = [ctx.ard2.light() for _ in range(5)]
    assert all(0 <= v <= 1023 for v in vals), f"ADC 범위 밖: {vals}"
    avg = sum(vals) / len(vals)
    ctx.note(f"조도 평균 {avg:.0f}/1023")
    if avg < 5 or avg > 1018:
        raise Warn(f"값이 끝에 붙어 있음({avg:.0f}) - 분압 저항/배선 확인")
    if ctx.interactive:
        input("    > 조도 센서를 손으로 가린 뒤 Enter ")
        covered = sum(ctx.ard2.light() for _ in range(5)) / 5
        assert abs(covered - avg) > 30, f"가려도 값 변화 없음 ({avg:.0f} -> {covered:.0f})"
        ctx.note(f"가렸을 때 {covered:.0f}")


@test("ARD2", "RGB LED")
def t_rgb(ctx):
    ctx.need("ard2")
    b = ctx.ard2
    for rgb in [(255, 0, 0), (0, 255, 0), (0, 0, 255)]:
        b.rgb(*rgb)
        time.sleep(0.4 * ctx.timing_scale)
    ctx.confirm("RGB LED 가 빨강→초록→파랑으로 바뀌었나요?")
    b.rgb(0, 0, 0)


# --------------------------------------------------------------------------
# 통합 시나리오 : 두 보드를 함께 사용
# --------------------------------------------------------------------------
@test("통합", "동시 동작(모터 회전 중 센서 읽기)")
def t_concurrent(ctx):
    """ARD1 이 블로킹 이동 중이어도 ARD2 는 독립적으로 응답해야 한다."""
    ctx.need("ard1", "ard2")
    result = {}

    def run_motor():
        try:
            ctx.ard1.step_move(1024, 12)
            ctx.ard1.step_move(-1024, 12)
            result["ok"] = True
        except Exception as e:  # 스레드 예외를 메인으로 전달
            result["err"] = e

    th = threading.Thread(target=run_motor)
    th.start()
    reads = 0
    try:
        while th.is_alive():
            ctx.ard2.light()
            ctx.ard2.lcd_print(1, f"reads={reads}")
            reads += 1
    finally:
        th.join()
    if "err" in result:
        raise result["err"]
    ctx.ard1.step_release()
    assert reads >= 3, f"모터 회전 중 ARD2 응답 {reads}회 - USB 전원/허브 확인"
    ctx.note(f"모터 회전 중 ARD2 응답 {reads}회")


@test("통합", "거리 → 서보 각도 + LED 색")
def t_distance_reaction(ctx):
    ctx.need("ard1", "ard2")
    try:
        d = ctx.ard2.distance_cm()
    except DeviceError:
        raise Skip("초음파 측정 실패(이전 테스트 참고)")
    angle = int(max(0, min(180, (d - 5) / 95 * 180)))      # 5~100cm -> 0~180°
    color = (255, 0, 0) if d < 15 else (255, 160, 0) if d < 40 else (0, 255, 0)
    ctx.ard2.servo(angle)
    ctx.ard1.led_fill(*color)
    ctx.ard2.lcd_print(0, f"DIST {d:5.1f}cm")
    ctx.note(f"{d:.1f}cm -> 서보 {angle}°, LED {color}")
    ctx.confirm(f"서보가 약 {angle}° 이고 LED 스트립이 거리에 맞는 색인가요?")
    ctx.ard1.led_clear()
    ctx.ard2.servo(90)


@test("통합", "환경 대시보드(온습도/조도 → LCD·RGB·도트·LED)")
def t_dashboard(ctx):
    ctx.need("ard1", "ard2")
    b = ctx.ard2
    try:
        t, h = b.dht()
    except DeviceError:
        raise Skip("DHT 읽기 실패(이전 테스트 참고)")
    lux = b.light()
    b.lcd_print(0, f"T{t:4.1f}C H{h:4.1f}%")
    b.lcd_print(1, f"LIGHT {lux:4d}")
    comfy = 18 <= t <= 28 and 30 <= h <= 70
    b.rgb(*((0, 255, 0) if comfy else (255, 0, 0)))
    smile = [0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C]
    frown = [0x3C, 0x42, 0xA5, 0x81, 0x99, 0xA5, 0x42, 0x3C]
    b.dot_pattern(smile if comfy else frown)
    ctx.ard1.led_brightness(max(8, min(255, 255 - lux // 4)))  # 어두울수록 밝게
    ctx.ard1.led_fill(255, 180, 100)
    ctx.note(f"쾌적={comfy}, LED 밝기={max(8, min(255, 255 - lux // 4))}")
    ctx.confirm("LCD 에 온습도/조도가 보이고 RGB·도트·LED 가 상태에 맞게 표시되나요?")
    ctx.ard1.led_clear()
    ctx.ard1.led_brightness(64)
    b.rgb(0, 0, 0)
    b.dot_clear()


@test("통합", "통신 안정성(PING 50회 x 2)")
def t_stress(ctx):
    ctx.need("ard1", "ard2")
    for dev in (ctx.ard1, ctx.ard2):
        lat = []
        for _ in range(50):
            t0 = time.monotonic()
            dev.ping()
            lat.append((time.monotonic() - t0) * 1000)
        ctx.note(f"{dev.link.name}: 평균 {statistics.mean(lat):.1f}ms / 최대 {max(lat):.1f}ms")
        if max(lat) > 200:
            raise Warn(f"{dev.link.name} 최대 지연 {max(lat):.0f}ms")


# --------------------------------------------------------------------------
def run(ctx, only=None, log=print):
    results = []
    for group, name, fn in TESTS:
        if only and group not in only:
            continue
        ctx.notes = []
        t0 = time.monotonic()
        try:
            fn(ctx)
            status, detail = "PASS", ""
        except Skip as e:
            status, detail = "SKIP", str(e)
        except Warn as e:
            status, detail = "WARN", str(e)
        except (AssertionError, DeviceError, DeviceTimeout) as e:
            status, detail = "FAIL", str(e)
        except Exception as e:  # 예상 못한 오류도 FAIL 로 기록하고 계속 진행
            status, detail = "FAIL", f"{type(e).__name__}: {e}"
        dur = time.monotonic() - t0
        info = "; ".join(filter(None, [detail] + ctx.notes))
        log(f"  [{status}] {group:4} {name} ({dur:.1f}s){'  - ' + info if info else ''}")
        results.append({"group": group, "name": name, "status": status,
                        "detail": detail, "notes": list(ctx.notes), "seconds": round(dur, 2)})
    return results
