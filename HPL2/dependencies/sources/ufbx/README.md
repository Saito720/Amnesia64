# ufbx

The unmodified `ufbx.c`, `ufbx.h`, and `LICENSE` files are from
[ufbx 0.23.0](https://github.com/ufbx/ufbx/tree/v0.23.0), commit
`fcc5d6ba444cfd3eb80677dba5e37e493941abe5`.

The library is available under the MIT license or the Unlicense; both are included
in `LICENSE`. It is compiled directly into HPL2 in every Visual Studio configuration,
with no separate download, binary library, or runtime DLL required.

`MeshLoaderFBX` uses ufbx in place of the original Autodesk FBX SDK. The original
AMFP importer targeted SDK 2012.2, whose supplied Windows libraries were built for
32-bit Visual C++ 2010. This fork uses the same source dependency for Win32 and x64.

The adapter retains AMFP's authored coordinates, clockwise faces, flipped UV V,
material names, and bind-pose conventions so imported resources can be used with
the game's existing `.msh` and `.anm` caches. Cache loading and saving follow the
engine's existing settings, including `ForceCacheLoadingAndSkipSaving`.

HPL still supports at most four skin influences per vertex and 256 indexed bones.
Its animation tracks store translation and rotation, not animated scale. The
adapter reports unsupported deformation modes; it retains the original loader's
handling of nondeforming bone markers and reports discarded marker scale. It does
not add morph targets, vertex-cache animation, or dual-quaternion skinning to HPL.

Upstream SHA-256 hashes:

| File | SHA-256 |
| --- | --- |
| `ufbx.h` | `942481725372d2ac4da5e77a062b47c20054a3440e7ee09a6043f99fe1f130ed` |
| `ufbx.c` | `7d8d6ae4373f71692f295ff49ee0826466306ebcaa80b0e587c13ed047b98cea` |
| `LICENSE` | `0dd48ebadf52273c736256325c8f078c03c8bb4facee22a4122de0ad3f615391` |

When updating, replace all three upstream files from one pinned revision, update
the version and hashes above, and validate mesh, skeleton, and animation loading
in both architectures. Keep local adaptation in HPL2's importer.
