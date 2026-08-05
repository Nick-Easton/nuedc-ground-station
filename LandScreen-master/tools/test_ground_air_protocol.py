#!/usr/bin/env python3
import importlib.util
import json
import math
import pathlib


SERVER_PATH = pathlib.Path(__file__).with_name("fake_ground_air_server.py")
SPEC = importlib.util.spec_from_file_location("fake_ground_air_server", SERVER_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def validate(payload):
    assert payload["type"] == "ground_air_telemetry"
    assert payload["source"]
    assert isinstance(payload["timestamp_ms"], int)
    for name in ("car", "drone"):
        pose = payload[name]
        assert isinstance(pose["valid"], bool)
        for field in ("x_m", "y_m", "yaw_deg", "speed_mps", "battery_percent"):
            assert math.isfinite(float(pose[field]))
    assert math.isfinite(float(payload["drone"]["z_m"]))
    assert payload["mission"]["state"]
    assert payload["mission"]["mode"] in ("DROP", "MOVING_LAND")
    for field in ("localization_ok", "car_link_ok", "drone_link_ok"):
        assert isinstance(payload["links"][field], bool)
    wire = (json.dumps(payload) + "\n").encode("utf-8")
    assert wire.endswith(b"\n")
    assert json.loads(wire)["mission"]["state"] == payload["mission"]["state"]


def main():
    expected_states = {
        "TAKEOFF",
        "HOVER_3S",
        "ACQUIRE_CAR",
        "FOLLOW_CAR",
        "ALIGN_AND_DROP",
        "DROP",
        "RETURN_HOME",
        "LANDING",
        "COMPLETE",
    }
    seen = set()
    for elapsed in range(0, 86):
        payload = MODULE.build_payload(float(elapsed))
        validate(payload)
        seen.add(payload["mission"]["state"])
        assert 0.0 <= payload["car"]["x_m"] <= 4.0
        assert 0.0 <= payload["car"]["y_m"] <= 5.0
    assert expected_states == seen
    print("ground-air protocol checks passed: {} states".format(len(seen)))


if __name__ == "__main__":
    main()
