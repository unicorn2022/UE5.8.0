# api/common

Shared types, macros, and utilities used across all Titan API libraries (`titan_api`, `act_api`, `mhc_api`).

## Files

### `Defs.h`
Core macro definitions for the Titan API:
- `TITAN_NAMESPACE` — maps to `epic::nls` (the internal C++ namespace for all Titan modules)
- `TITAN_API_NAMESPACE` — maps to `titan::api` (the public API namespace)
- `TITAN_API` — DLL export/import annotation (`__declspec(dllexport/dllimport)` on MSVC, `__attribute__((visibility("default")))` on GCC/Clang). Controlled by `TITAN_DYNAMIC_API` / `TITAN_STATIC_API` preprocessor flags.
- `TITAN_CHECK_OR_RETURN(condition, returnValue, ...)` — asserts a condition at runtime; on failure sets an error via `sc::StatusProvider` and returns `returnValue`.
- `TITAN_HANDLE_EXCEPTION(...)` — records an error and returns `false`; used in catch blocks.

### `Common.h`
Includes `Defs.h` and pulls in the Titan logging and error-reporting headers (`carbon/common/Log.h`, `status/Provider.h`). Every API translation unit includes this.

### `LandmarkData.h` / `LandmarkData.cpp`
`FaceTrackingLandmarkData` — the universal landmark type passed into all API functions:
- Per-camera 2D landmark positions (distorted image coordinates)
- 3D landmark positions (for scan-based workflows)
- Confidence weights per landmark

### `OpenCVCamera.h`
`OpenCVCamera` — camera model compatible with OpenCV conventions:
- Intrinsics (focal length, principal point)
- Distortion coefficients (radial + tangential)
- Extrinsics (rotation + translation — camera-to-world or world-to-camera, depending on context)

Used everywhere cameras are passed into the API (face tracking, actor creation, stereo reconstruction, calibration).

### `Internals/OpenCVCamera2MetaShapeCamera.h`
Internal helper that converts an `OpenCVCamera` to Titan's internal `MetaShapeCamera` format. Not part of the public API surface.
