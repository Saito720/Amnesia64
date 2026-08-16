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

## Ellipsoidal atmosphere

`Atmosphere` is a translucent material type for an origin-centered atmosphere
proxy viewed from space. It ray-marches Rayleigh and Mie light through
concentric ground and outer ellipsoids, uses the first active `SunLight`, and
includes planetary shadowing. An optional six-layer 2D-array diffuse texture
supplies a global cloud height field. Cloud-map luminance drives coverage,
optical density, and cloud-top height inside a shallow lower-atmosphere volume.
The material intentionally renders only the outward-facing side of the proxy
and therefore does not support a camera inside the atmosphere.

The defaults match the metre-space coordinates written by this project's GLB
Earth assets. HPL uses Y as the polar axis:

```xml
<Material>
  <Main Type="atmosphere" PhysicsMaterial="Silent" DepthTest="true"
        BlendMode="Mul" UseAlpha="false" />
  <TextureUnits>
    <Diffuse File="clouds.ktx2" Type="2DArray" MipMaps="true"
             Wrap="ClampToEdge" Compress="true" />
  </TextureUnits>
  <SpecificVariables>
    <Var Name="GroundRadii" Value="6378150 6356750 6378150" />
    <Var Name="AtmosphereRadii" Value="6478150 6456750 6478150" />
    <Var Name="RayleighScattering" Value="0.000005802 0.000013558 0.0000331" />
    <Var Name="MieScattering" Value="0.000003996" />
    <Var Name="MieExtinction" Value="0.00000444" />
    <Var Name="RayleighScaleHeight" Value="8000" />
    <Var Name="MieScaleHeight" Value="1200" />
    <Var Name="MieAnisotropy" Value="0.8" />
    <Var Name="AerosolDensity" Value="2" />
    <Var Name="Exposure" Value="1.6" />
    <Var Name="MultipleScatteringStrength" Value="0.85" />
    <Var Name="CloudArrayTileGrid" Value="3 2" />
    <Var Name="CloudBaseHeight" Value="1200" />
    <Var Name="CloudMaxHeight" Value="10000" />
    <Var Name="CloudCoverageThreshold" Value="0.065" />
    <Var Name="CloudExtinction" Value="0.00020" />
    <Var Name="CloudAmbient" Value="0.2" />
    <Var Name="CloudSunIntensity" Value="0.75" />
    <Var Name="CloudTwilightStrength" Value="0.055" />
    <Var Name="CloudTwilightColor" Value="0.4 0.58 0.88" />
    <Var Name="CloudSelfShadow" Value="1.4" />
    <Var Name="CloudShadowStrength" Value="0.65" />
    <Var Name="CloudShadowSoftness" Value="1.5" />
  </SpecificVariables>
</Material>
```

The radii, scale heights, and inverse-distance scattering coefficients must all
use the proxy mesh's local distance unit. Apply the atmosphere object's scale
before export so that its local-space radii match the material values.

The atmosphere is composed in two translucent draws. The first multiplies the
existing framebuffer by physical RGB transmittance; the second additively
composes atmospheric and cloud in-scattering. The two shader programs are
specialized when loaded, so each draw contains only the calculations it uses.
This avoids the grayscale attenuation imposed by ordinary alpha blending and
gives the surface the wavelength-dependent softening visible near the limb.

`MultipleScatteringStrength` adds a low-order isotropic approximation of
light redirected by additional atmospheric scattering events. It reuses the
Sun-visible scattering source already integrated by the shader, so it grows
with optical path length and fades in planetary shadow. `0` preserves the
single-scattering solution; the validated Earth baseline uses `0.85`.

`AerosolDensity` scales the Mie density profile without changing its scale
height or single-scattering albedo. Both Mie scattering and extinction increase
together, providing a physically coupled low-altitude haze control. `1`
preserves the written scattering coefficients; the Earth baseline uses `2`.

The cloud height effect is volumetric shader displacement rather than vertex
displacement: it changes where density exists along the view ray, producing
parallax and a raised limb without modifying the atmosphere mesh. The diffuse
texture must be a 2D array whose tiles reconstruct the equirectangular map from
left to right and top to bottom; the default layout is three columns by two
rows. Materials without a compatible diffuse texture continue to render the
clear atmosphere only.

Cloud sampling explicitly filters across the equirectangular longitude seam.
At grazing angles it adds a capped mip bias and compensates the averaged cloud
coverage, suppressing sub-pixel shimmer and the bright horizon band without
cutting a cloud-free halo into the limb.

Cloud lighting uses the directional Sun, an ambient term, and an inexpensive
map-aware self-shadow approximation. The cloud volume contributes both
extinction and in-scattered light while sharing the atmosphere's two-pass
composition.

Projected cloud shadows are evaluated only for camera rays that reach the
ground. The multiplicative atmosphere pass traces from that ground point toward
the Sun through the mapped cloud layer, so the shadows follow both cloud height
and solar direction without tinting the atmosphere or the clouds themselves.
`CloudShadowStrength` controls the fraction of surface illumination affected;
`CloudShadowSoftness` adds texture mip bias to keep the projected mask from
appearing unnaturally sharp.

## Planet surface controls

Solid materials with a specular map automatically enable the Earth-specific
Sun highlight when any of the first three values differs from its neutral
value. Planetary twilight is an independent, opt-in surface-lighting effect:

```xml
<Var Name="SpecularIntensityScale" Value="0.8" />
<Var Name="SpecularGlossBias" Value="0.05" />
<Var Name="OceanSpecularBroadStrength" Value="0.1" />
<Var Name="PlanetaryTwilightStrength" Value="0.055" />
```

The first two values remap the existing specular texture. The broad-strength
control adds one low-energy highlight shoulder suitable for a planet-scale
ocean; it does not attempt to resolve individual waves. Planetary twilight
adds the low-energy sky fill and warm sunset tail used by the surface near the
terminator. Leaving these values at `1`, `0`, `0`, and `0` respectively keeps
the standard solid-material path.
