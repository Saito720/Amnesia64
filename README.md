# Amnesia 64
64-bit Windows port of Amnesia: The Dark Descent

## Key changes:
- Can be compiled in both 32-bit and 64-bit modes using VS2019 with latest build tools.
- Single solution file for all projects (main game, HPL2, dependencies and editors). No need to compile the engine separately.
- Produces self-contained .exe files without dependency on 3rd party dlls (this prevents cluttering user's game folder with 64-bit dlls).
- Some libraries were changed, most notably:
	- SDL2 was upgraded from 2.0.4 to 2.0.12
	- alut was replaced with freealut
	- Newton Dynamics was upgraded from 2.08 to 2.32 (I simply couldn't find the source code for 2.08)
	- Fbx support is temporarily removed (I'm planning to re-implement it using OpenFBX)

## Astronomical Sun light

`SunLight` is a global, shadowless directional light whose direction is derived
from the current UTC time. It uses a right-handed, Y-up Earth-fixed convention:

- +Y is geographic north.
- +X is latitude 0 degrees, longitude 0 degrees (Greenwich).
- -Z is latitude 0 degrees, longitude 90 degrees east.

Add a Sun directly to a map's `Entities` section:

```xml
<SunLight Active="true" CastShadows="false" DiffuseColor="1 1 1 1"
          HighlightKnee="0.7" ID="1000" Intensity="2.5" Name="Sun"
          ShowSunDisk="true" UseSystemTime="true"
          Rotation="0 0 0" Scale="1 1 1" WorldPos="0 0 0" />
```

`Intensity` is a non-negative multiplier for the Sun's diffuse and specular
contribution. It defaults to `1.0`; `2.5` is a stronger starting point for the
Earth albedo material while keeping `DiffuseColor` available for tinting. New
Sun lights created in the Level Editor start at `2.5`; the editor exposes the
intensity, highlight knee, disk visibility, system-time toggle, and fixed
Julian date on a dedicated Sun tab.

`HighlightKnee` controls the Sun's hue-preserving soft highlight shoulder in
HPL2's LDR accumulation buffer. Values below the knee are unchanged; brighter
values are smoothly compressed toward white instead of hard-clipping. The
default is `0.7`; use `1.0` to disable compression.

`ShowSunDisk` controls a camera-relative disk rendered at the Sun's mean
apparent diameter of approximately 0.533 degrees. The disk is derived from the
same astronomical direction as the light, remains behind solid geometry, and
defaults to `true`.

For deterministic orbital tests, set `UseSystemTime="false"` and provide a
double-precision Julian date, for example `JulianDate="2460389.62916667"`.
Sunlight affects normally lit solid materials; remove the `Unlit` material
variable when testing it.

The 32-bit deferred mode uses a hybrid G-buffer: albedo and packed-depth
attachments remain RGBA8, while the camera-space normal attachment uses
RGBA16F. This prevents camera-relative lighting bands on large, smoothly
curved surfaces without doubling the precision of every G-buffer texture.
