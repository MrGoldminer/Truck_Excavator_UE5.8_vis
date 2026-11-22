"""QuarryWorks MQTT management-plane simulator.

Acts as the local management server: subscribes to telemetry/task/test topics,
dispatches tasks (drive_to / dig / haul / test_suite), and synchronously waits
for results — the "task dispatch + sync test completion" loop.

Usage (requires `pip install paho-mqtt`):
    python mqtt_manager_sim.py --suite          # dispatch test_suite, wait for summary
    python mqtt_manager_sim.py --drive 12 -3    # dispatch drive_to and wait
    python mqtt_manager_sim.py --watch          # just print telemetry
"""
import argparse
import json
import sys
import threading
import time
import uuid

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit("Missing dependency: run `pip install paho-mqtt`")

PREFIX = "quarryworks/v1"


class Manager:
    def __init__(self, host, port):
        self.host, self.port = host, port
        self.client = mqtt.Client(client_id=f"mgr-{uuid.uuid4().hex[:8]}", protocol=mqtt.MQTTv311)
        self.client.on_message = self._on_message
        self.lock = threading.Lock()
        self.last_telemetry = None
        self.case_results = []
        self.summary = None
        self.task_results = {}
        self.events = {"task": threading.Event(), "summary": threading.Event()}

    def _on_message(self, client, userdata, msg):
        payload = msg.payload.decode("utf-8", "replace")
        try:
            data = json.loads(payload)
        except json.JSONDecodeError:
            return
        if msg.topic.endswith("/telemetry"):
            with self.lock:
                self.last_telemetry = data
        elif msg.topic.endswith("/test/result"):
            if data.get("type") == "test_suite_summary":
                with self.lock:
                    self.summary = data
                self.events["summary"].set()
            elif "case" in data:
                status = "PASS" if data.get("pass") else "FAIL"
                metrics = data.get("metrics", {})
                print(f"  [{status}] {data['case']}: {json.dumps(metrics, ensure_ascii=False)} "
                      f"({data.get('duration_s', 0):.1f}s)")
        elif msg.topic.endswith("/task/result"):
            with self.lock:
                self.task_results[data.get("task_id", "anon")] = data
            self.events["task"].set()
            print(f"  task {data.get('task_id', 'anon')} -> {data.get('result', '?')} ({data.get('reason', '')}) "
                  f"elapsed={data.get('elapsed_s', 0):.1f}s delivered={data.get('delivered_mass_kg', 0):.0f} kg")

    def connect(self):
        self.client.connect(self.host, self.port, keepalive=30)
        for suffix in ("/telemetry", "/task/result", "/test/result", "/status"):
            self.client.subscribe(PREFIX + suffix, qos=0)
        self.client.loop_start()

    def dispatch(self, task: dict):
        task["task_id"] = task.get("task_id") or uuid.uuid4().hex[:8]
        self.client.publish(PREFIX + "/task", json.dumps(task), qos=1)
        return task["task_id"]

    def await_event(self, name, timeout):
        return self.events[name].wait(timeout)

    def print_telemetry(self):
        with self.lock:
            t = self.last_telemetry
        if not t:
            return
        vehicles = "  ".join(
            f"{v['id']}:{v['speed_kmh']:.1f}km/h g{v.get('gear', '?')} rpm{v.get('engine_rpm', 0):.0f} "
            f"ground{v.get('grounded_wheel_count', '?')}/{len(v.get('wheels', []))}"
            for v in t.get("vehicles", []))
        print(f"sim={t.get('simulation_time_s', 0):8.2f}s delivered={t.get('delivered_mass_kg', 0) / 1000:6.2f}t  {vehicles}")


def main():
    parser = argparse.ArgumentParser(description="QuarryWorks MQTT management simulator")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--suite", action="store_true", help="dispatch test_suite and wait for the summary")
    parser.add_argument("--drive", nargs=2, type=float, metavar=("X", "Y"), help="dispatch drive_to in meters")
    parser.add_argument("--dig", action="store_true", help="dispatch a dig cycle")
    parser.add_argument("--haul", action="store_true", help="dispatch a haul cycle")
    parser.add_argument("--watch", action="store_true", help="print telemetry only")
    parser.add_argument("--timeout", type=float, default=200.0)
    args = parser.parse_args()

    mgr = Manager(args.host, args.port)
    mgr.connect()
    print(f"manager connected to {args.host}:{args.port}; subscribing {PREFIX}/*")
    time.sleep(1.0)
    if not mgr.client.is_connected():
        print("ERROR: broker not reachable — is the local MQTT server running?")
        return 1

    if args.watch:
        try:
            while True:
                mgr.print_telemetry()
                time.sleep(1.0)
        except KeyboardInterrupt:
            return 0

    if args.drive:
        x, y = args.drive
        print(f"dispatch drive_to x={x} y={y}")
        mgr.dispatch({"type": "drive_to", "vehicle": "mining_truck", "x": x, "y": y, "tolerance": 2.5})
        mgr.await_event("task", args.timeout)
    if args.dig:
        print("dispatch dig")
        mgr.dispatch({"type": "dig"})
        mgr.await_event("task", args.timeout)
    if args.haul:
        print("dispatch haul")
        mgr.dispatch({"type": "haul"})
        mgr.await_event("task", args.timeout)
    if args.suite:
        print("dispatch test_suite (6 cases) — waiting for the summary…")
        start = time.time()
        mgr.dispatch({"type": "test_suite"})
        if mgr.await_event("summary", args.timeout):
            with mgr.lock:
                s = mgr.summary
            print(f"SUITE DONE in {time.time() - start:.0f}s: {'ALL PASS' if s.get('overall_pass') else 'FAILURES'} "
                  f"({s.get('passed', 0)}/{s.get('total', 0)})")
        else:
            print(f"SUITE TIMEOUT after {args.timeout:.0f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
