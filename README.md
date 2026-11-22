# QuarryWorks

UE 5.8 C++ quarry simulation with a drivable excavator and mining truck, deformable sand terrain, cargo transfer, delivery tracking, and Protobuf commands/telemetry.

The vehicle model is force-driven (CARLA-style): per-wheel raycast spring/damper suspension, simplified Pacejka tire forces clamped by a friction circle, an engine torque curve with an automatic gearbox for the truck, and hydraulic skid steering for the excavator. Digging rate scales with bucket tip speed, cut angle, and material hardness. A TCP bridge exposes CARLA-style remote control and telemetry streaming to external Python clients.

## Run

1. Install UE 5.8 and Visual Studio 2022 with the C++ workload.
2. Install Git LFS and run `git lfs install`.
3. Open `QuarryWorks.uproject`, build the `QuarryWorksEditor` target, then play `Content/Levels/QuarryLevel.umap`.
4. For the best MSVC compatibility, keep the project in an ASCII-only path.

The project enables UE's official `ModelContextProtocol` and `AllToolsets` plugins. `.mcp.json` points to `http://127.0.0.1:8000/mcp`; the server starts automatically in the editor.

## Controls

- `W/S`: throttle and reverse
- `A/D`: steering
- `Space`: service brake
- `Left Shift`: parking brake while held
- Mouse: camera
- `Ctrl+1` or `Ctrl+Numpad1`: excavator
- `Ctrl+2` or `Ctrl+Numpad2`: mining truck
- `V`: cycle vehicles when they are nearby

Excavator:

- `Q/E`: rotate upper structure
- `R/F`: boom
- `T/G`: stick
- `Y/H`: bucket
- Hold left mouse: dig near the bucket tip (sweep the bucket through the pile at a good cut angle; a static or badly angled bucket loads nothing)
- Hold right mouse: transfer material into a nearby truck bed

Mining truck:

- `X`: raise the bed and unload at the delivery bay; press again to cancel

## Simulation model

`UQuarryVehicleMovementComponent` (see `Source/QuarryWorks/Public/Vehicles/QuarryVehicleMovementComponent.h`) replaces velocity scripting with physics forces:

- Suspension: spring + damper per wheel (`SuspensionStiffness/Damping`, auto-calibrated from vehicle mass; anti-roll bars via `AntiRollStiffness`, negative = auto). Load transfer, brake dive, and body roll emerge naturally.
- Tires: longitudinal slip ratio and slip angle feed a simplified Pacejka curve (`F = mu*N*sin(C*atan(B*slip))`), jointly clamped by the friction circle. Loose dig-site terrain uses `LooseMaterialFriction` (default 0.4) so heavy trucks dig in and spin; solid ground uses `GroundFriction`.
- Powertrain (wheeled): piecewise-linear engine torque curve, 5-speed automatic gearbox with shift interruption, reverse gear, engine braking, and a soft speed limiter. Wheel spin integrates drive torque against tire reaction, so wheels break traction realistically.
- Tracked (excavator): per-side hydraulic speed servos with differential targets, lateral track restraint, and soil turn resistance; full pivot turns are supported.
- `bLegacyKinematic = true` (per vehicle in the pbtxt config) restores the original kinematic movement as a fallback.

Dig mechanics (`AQuarryExcavator`): effective rate = `work_rate_kg_s * speedFactor * cutAngleFactor * MaterialHardness`. The bucket tip must sweep faster than `MinDigSpeedCmS`, the bucket pitch should stay within `CutAngleToleranceDeg` of `OptimalCutAngleDeg`, and harder materials (gravel 0.7, ore 0.45) dig slower.

IMU: `UQuarryImuSensorComponent` on every vehicle samples specific force (gravity removed), gyroscope angular velocity, and world acceleration at a fixed rate with configurable bias/noise.

Determinism: physics substepping runs at up to 120 Hz (`Config/DefaultEngine.ini`). `UQuarryWorksiteSubsystem` exposes CARLA-style synchronous stepping (`SetSynchronousMode`, `SetFixedDeltaTime`): commands apply at tick boundaries and telemetry snapshots emit at a fixed cadence with a monotonic simulation clock.

## External control (CARLA-style client/server)

In the editor console:

```
Quarry.Server.Start          # TCP protobuf bridge on 127.0.0.1:17000
Quarry.Server.Start 17500    # custom port
Quarry.Server.Stop
Quarry.Mqtt.Start            # MQTT management plane (host/port from DefaultGame.ini)
Quarry.Mqtt.Stop
```

Wire format (TCP): `[uint32 little-endian length][protobuf payload]`. The client sends `VehicleCommand` frames (including `ExcavatorCommand` actuator control); the server streams `QuarryTelemetry` frames every tick (or every fixed snapshot in synchronous mode).

Example clients (from `Scripts/`; `pip install protobuf paho-mqtt`):

```
python quarry_client.py                     # TCP telemetry viewer
python quarry_client.py --drive             # remote-drive the truck in a slalom
python mqtt_manager_sim.py --watch          # MQTT telemetry monitor
python mqtt_manager_sim.py --drive 12 -3    # dispatch a drive_to task (meters)
python mqtt_manager_sim.py --suite          # run the 6-case test suite, wait for summary
```

## MQTT management plane

`Scripts/mqtt_manager_sim.py` stands in for the local management server. Topics (prefix `quarryworks/v1/`, configurable):

- `telemetry` (20 Hz QoS 0): CARLA-named JSON — location/velocity in meters, `imu.accelerometer`/`imu.gyroscope` (m/s², rad/s), `imu.compass` (rad), `gnss.{lat,lon,alt}`, per-wheel `slip_ratio`/`load_n`, cargo, RPM/gear.
- `command` (QoS 1): CARLA `VehicleControl` JSON — `{vehicle_id, throttle, steer, brake, hand_brake, reverse}`.
- `task` (QoS 1): `{type: drive_to|dig|haul|test_suite, vehicle, x, y, tolerance, task_id}` — targets in meters.
- `task/result`, `test/result`: synchronous completion reporting; the test suite publishes each case (`{case, pass, metrics}`) plus a final `{type: test_suite_summary, overall_pass, passed, total}`.
- `status`: retained online/offline (MQTT LWT on disconnect).

Field-by-field CARLA mapping and known deltas: [Docs/CARLA_ALIGNMENT.md](Docs/CARLA_ALIGNMENT.md).

## Real vehicle models

Both machines use real static meshes from the ConstructionMachines pack (`F:\code\工程车辆1`, Epic official free content, copied to `Content/ConstructionMachines/`):

- **Mining truck**: `SM_AmericanDumpTruck` body (auto-fitted to 650 cm) + six `SM_Wheel_01` wheels at MEASURED positions (vertex clustering: front axle +308, rear tandem -96/-181, track ±92.5, radius 47).
- **Excavator**: `SM_ExcavatorTracks` — the complete tracked machine (cab + boom + tracks in one mesh, auto-fitted to 650 cm), replacing all blockout visuals; the dig arm articulates invisibly via `BucketTip` so the dig mechanics still work.

`UQuarryVehicleMeshComponent` holds every placement knob (body path/yaw/fit/Z-offset, wheel mounts/radius/mesh-scale, wheel spin axis/sign). All values are MEASURED from mesh bounds and vertex clustering — see `Docs/VEHICLE_ORIENTATION.md` for the orientation conventions, the measurement tables, and how to re-measure.

## Configuration

`Config/QuarryWorks.pbtxt` is a text-format Protobuf file. It controls each vehicle's mass, engine/brake force, speed limits, cargo capacity, work rate, and force-model overrides (`engine_peak_torque_nm`, `tire_friction`, `loose_material_friction`, `track_yaw_rate_deg_s`, `legacy_kinematic`). Changes take effect when the play session starts.

`Proto/quarryworks.proto` defines:

- `VehicleCommand`: vehicle id, throttle, steering, service/parking brake, plus `ExcavatorCommand` actuators
- `VehicleTelemetry`: transforms, velocity, orientation quaternion, angular velocity, IMU, engine RPM, gear, grounded wheel count, tire slip flag, and per-wheel state (slip ratio/angle, load, forces, contact friction)
- `QuarryTelemetry`: sequence, simulation time, delivered mass, vehicles
- `QuarryConfig`: versioned vehicle parameters

`UQuarryWorksiteSubsystem::GetTelemetryBytes` and `ApplyVehicleCommand` are the process-local binary message boundary. `GetTelemetryDebugString` provides a decoded diagnostic view. The TCP server uses the same messages.

After editing the schema, install Protobuf 30.0 and run `Proto/Generate.bat`. The script prefers `Build/Tools/Protoc/bin/protoc.exe` (git-ignored; download `protoc-30.0-win64.zip` from protobuf releases and extract it there) and falls back to `protoc` on PATH. Generated sources are included so Protoc is not required for a normal build. The Python bindings for the example client are generated into `Scripts/quarryworks_pb2.py`.

The module links UE's Protobuf 30.0 libraries. Matching Abseil 20240722.0 and Protobuf 30.0 headers are included under `ThirdParty` because the installed UE distribution may omit required `.inc` files.

## Verifying the simulation upgrade

1. Build `QuarryWorksEditor` (Development Editor) and play `QuarryLevel`.
2. Truck, empty, full throttle on solid ground: observe gear-up shifts on the HUD, mild acceleration, wheel spin only on loose terrain.
3. Load the truck full at the dig site: braking now pitches the chassis forward (load transfer), and hard acceleration from standstill on the sand pile triggers `TIRE SLIP` on the HUD.
4. Corner hard at speed: body roll appears; excessive speed can roll the truck over (physics, not scripted).
5. Excavator: pivot-turn in place (`A/D`), note it slows when standing on the loose pile (low friction).
6. Dig: hold LMB and sweep the bucket (boom/stick/bucket together); a static bucket loads nothing, the HUD shows the bucket tip speed, and bad cut angles stop the dig with a hint.
7. TCP: run `Quarry.Server.Start`, then `python Scripts/quarry_client.py --drive` from another terminal; telemetry (RPM, gear, wheels, IMU) streams and the truck drives itself in a slalom.

## Structure

- `Source/QuarryWorks/Public/Vehicles`: vehicle, movement, and visual types
- `Source/QuarryWorks/Public/Sensors`: IMU ground-truth sensor
- `Source/QuarryWorks/Public/Cargo`: cargo and material types
- `Source/QuarryWorks/Public/Terrain`: deformable digging terrain
- `Source/QuarryWorks/Public/Worksite`: game mode, player controller, configuration, HUD, and worksite state (sync mode + TCP server integration)
- `Source/QuarryWorks/Public/Protocol`: generated messages, protocol helpers, and the TCP server
- `Proto`: schema and regeneration script
- `Scripts`: Python example client and generated protobuf bindings
- `Config/QuarryWorks.pbtxt`: runtime vehicle parameters
- `Docs/Images`: captured runtime views

Only the assets required by the quarry example are tracked. The original animation sample assets can remain in the local `Content` directory without being pushed.

![QuarryWorks](Docs/Images/QuarryWorks.png)
