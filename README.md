# QuarryWorks

UE 5.8 C++ quarry simulation with a drivable excavator and mining truck, deformable sand terrain, cargo transfer, delivery tracking, and Protobuf commands/telemetry.

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
- Hold left mouse: dig near the bucket tip
- Hold right mouse: transfer material into a nearby truck bed

Mining truck:

- `X`: raise the bed and unload at the delivery bay; press again to cancel

## Configuration

`Config/QuarryWorks.pbtxt` is a text-format Protobuf file. It controls each vehicle's mass, engine/brake force, speed limits, cargo capacity, and work rate. Changes take effect when the play session starts.

`Proto/quarryworks.proto` defines:

- `VehicleCommand`: vehicle id, throttle, steering, service brake, and parking brake
- `QuarryTelemetry`: vehicle transforms, speed, cargo, and delivered mass
- `QuarryConfig`: versioned vehicle parameters

`UQuarryWorksiteSubsystem::GetTelemetryBytes` and `ApplyVehicleCommand` are the process-local binary message boundary. `GetTelemetryDebugString` provides a decoded diagnostic view. Network transport can use the same messages without changing gameplay code.

After editing the schema, install Protobuf 30.0 and run `Proto/Generate.bat`. Generated sources are included so Protoc is not required for a normal build.

The module links UE's Protobuf 30.0 libraries. Matching Abseil 20240722.0 and Protobuf 30.0 headers are included under `ThirdParty` because the installed UE distribution may omit required `.inc` files.

## Structure

- `Source/QuarryWorks/Public/Vehicles`: vehicle, movement, and visual types
- `Source/QuarryWorks/Public/Cargo`: cargo and material types
- `Source/QuarryWorks/Public/Terrain`: deformable digging terrain
- `Source/QuarryWorks/Public/Worksite`: game mode, player controller, configuration, HUD, and worksite state
- `Source/QuarryWorks/Public/Protocol`: generated messages and protocol helpers
- `Proto`: schema and regeneration script
- `Config/QuarryWorks.pbtxt`: runtime vehicle parameters
- `Docs/Images`: captured runtime views

Only the assets required by the quarry example are tracked. The original animation sample assets can remain in the local `Content` directory without being pushed.

![QuarryWorks](Docs/Images/QuarryWorks.png)
