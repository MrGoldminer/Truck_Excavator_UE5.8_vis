"""QuarryWorks CARLA-style TCP client example.

The UE project acts as the TCP server (console command ``Quarry.Server.Start``,
default port 17000). This client receives length-prefixed QuarryTelemetry
frames and can send VehicleCommand frames back, including remote excavator
actuator control.

Usage:
    python quarry_client.py                     # telemetry viewer
    python quarry_client.py --drive             # remote-drive the truck (slalom)
    python quarry_client.py --drive --vehicle excavator --dig

Options:
    --host 127.0.0.1    server address
    --port 17000        server port
    --vehicle NAME      excavator | mining_truck (default mining_truck)
    --dig               hold the dig command (excavator only)

Requires: pip install protobuf
Run from this directory (or add it to PYTHONPATH) so quarryworks_pb2 resolves.
"""
import argparse
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    import quarryworks_pb2
except ImportError:
    sys.exit("Missing dependencies: run `pip install protobuf` from this directory.")


def recv_exact(sock, count):
    buf = b""
    while len(buf) < count:
        chunk = sock.recv(count - len(buf))
        if not chunk:
            raise ConnectionError("server closed the connection")
        buf += chunk
    return buf


def recv_telemetry(sock):
    (length,) = struct.unpack("<I", recv_exact(sock, 4))
    return quarryworks_pb2.QuarryTelemetry.FromString(recv_exact(sock, length))


def send_command(sock, command):
    payload = command.SerializeToString()
    sock.sendall(struct.pack("<I", len(payload)) + payload)


def describe(telemetry):
    lines = [f"sim={telemetry.simulation_time_s:8.2f}s  delivered={telemetry.delivered_mass_kg / 1000.0:6.2f} t"]
    for vehicle in telemetry.vehicles:
        wheel_map = "".join("W" if wheel.in_contact else "." for wheel in vehicle.wheels)
        imu = vehicle.imu
        specific_force = imu.specific_force_m_s2
        abs_force = sum(abs(c) for c in (specific_force.x, specific_force.y, specific_force.z))
        lines.append(
            f"  {vehicle.vehicle_id:12s} {vehicle.speed_kmh:5.1f} km/h  gear={vehicle.gear:2d}  "
            f"rpm={vehicle.engine_rpm:5.0f}  ground={vehicle.grounded_wheel_count}/{len(vehicle.wheels)} "
            f"[{wheel_map}]  slip={int(vehicle.any_wheel_slipping)}  "
            f"cargo={vehicle.cargo.current_mass_kg:7.0f}/{vehicle.cargo.capacity_kg:.0f} kg"
        )
        lines.append(
            f"  {'':12s} pos=({vehicle.location_cm.x:7.0f},{vehicle.location_cm.y:7.0f},"
            f"{vehicle.location_cm.z:6.0f})  |f|={abs_force:5.1f}"
        )
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="QuarryWorks telemetry/control client")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17000)
    parser.add_argument("--vehicle", default="mining_truck", choices=["excavator", "mining_truck"])
    parser.add_argument("--drive", action="store_true", help="send remote driving commands")
    parser.add_argument("--dig", action="store_true", help="hold the dig command (excavator)")
    args = parser.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=5.0)
    sock.settimeout(0.5)
    print(f"connected to {args.host}:{args.port}  (Ctrl+C to quit)")
    start = time.time()
    received = 0
    try:
        while True:
            if args.drive:
                command = quarryworks_pb2.VehicleCommand()
                command.vehicle_id = args.vehicle
                # Square-wave steering demo: the force-driven model will show
                # speed fade, load transfer, and tire slip in the telemetry.
                elapsed = time.time() - start
                command.throttle = 0.6
                command.steering = 0.55 if int(elapsed) % 8 < 4 else -0.55
                if args.dig:
                    command.excavator.dig = True
                send_command(sock, command)
            try:
                telemetry = recv_telemetry(sock)
            except socket.timeout:
                continue
            received += 1
            if received % 15 == 1:
                print(describe(telemetry))
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
