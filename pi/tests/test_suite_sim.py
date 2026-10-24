"""가상 아두이노로 테스트 러너 자체를 검증한다 (하드웨어 불필요)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from hwtest import suite  # noqa: E402
from hwtest.__main__ import main  # noqa: E402
from hwtest.link import Arduino1, Arduino2, ArduinoLink  # noqa: E402
from hwtest.simulator import SimArduino1, SimArduino2  # noqa: E402

SCALE = 0.02


def make_ctx(ard1=None, ard2=None):
    return suite.Context(
        ard1=Arduino1(ArduinoLink(ard1, "ARD1")) if ard1 else None,
        ard2=Arduino2(ArduinoLink(ard2, "ARD2")) if ard2 else None,
        timing_scale=SCALE,
    )


def by_name(results):
    return {r["name"]: r["status"] for r in results}


def test_all_pass_with_healthy_boards():
    results = suite.run(make_ctx(SimArduino1(time_scale=SCALE), SimArduino2(time_scale=SCALE)), log=lambda *_: None)
    bad = [r for r in results if r["status"] not in ("PASS",)]
    assert not bad, bad


def test_missing_board_skips_its_tests():
    results = suite.run(make_ctx(ard2=SimArduino2(time_scale=SCALE)), log=lambda *_: None)
    for r in results:
        if r["group"] in ("ARD1", "통합"):
            assert r["status"] == "SKIP", r
        else:
            assert r["status"] == "PASS", r


def test_faults_are_reported_as_fail():
    ctx = make_ctx(SimArduino1(time_scale=SCALE),
                   SimArduino2(time_scale=SCALE, dht_fail=True, ultrasonic_fail=True))
    s = by_name(suite.run(ctx, log=lambda *_: None))
    assert s["온습도 센서(DHT)"] == "FAIL"
    assert s["초음파 거리 센서"] == "FAIL"
    assert s["거리 → 서보 각도 + LED 색"] == "SKIP"
    assert s["LED 스트립 색상"] == "PASS"


def test_unresponsive_board_times_out():
    ctx = make_ctx(SimArduino1(time_scale=SCALE, mute=True))
    s = by_name(suite.run(ctx, only={"ARD1"}, log=lambda *_: None))
    assert set(s.values()) == {"FAIL"}, s


def test_light_sensor_stuck_warns():
    ctx = make_ctx(ard2=SimArduino2(time_scale=SCALE, light=1023))
    assert by_name(suite.run(ctx, only={"ARD2"}, log=lambda *_: None))["조도 센서"] == "WARN"


def test_dashboard_drives_both_boards():
    a1, a2 = SimArduino1(time_scale=SCALE), SimArduino2(time_scale=SCALE, temp=35.0)
    ctx = make_ctx(a1, a2)
    suite.t_dashboard(ctx)
    assert a2.lcd[0].startswith("T35.0C")
    assert a2.rgb == (0, 0, 0)  # 테스트 끝에 원복


def test_cli_sim_exit_code(tmp_path):
    out = tmp_path / "r.json"
    assert main(["--sim", "--json", str(out)]) == 0
    assert out.exists()
