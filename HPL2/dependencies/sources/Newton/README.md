# Newton diagnostics

This vendored Newton version uses Microsoft CRT `_ASSERTE` and `_ASSERT`
checks for its internal diagnostics. Some collision checks can repeatedly open
modal assertion dialogs in Debug builds. Amnesia disables these Newton checks
by default, including when the Debug library is linked into a test harness.

The switch is confined to Newton's private `core/dgTypes.h` header. Debug
symbols, the Debug runtime, other Newton Debug code, and engine/game/test
assertions remain enabled. It does not change the process-wide CRT reporting
mode, and disabling a check does not repair a physics error.

To restore Newton's original assertion behavior for a Windows Debug build, add
`/p:NewtonEnableAssertions=1` to the MSBuild command used to build the solution
or game project. Use `0` (the default) to disable them again. Rebuild/relink the
game or harness after changing the setting; an existing executable keeps the
behavior of the library it was linked with. Release builds retain their
original disabled assertion behavior.

For a build that compiles Newton without this Visual Studio project, define
`NEWTON_ENABLE_ASSERTS=1` on the Newton compiler command line to opt in. The
vendored library's original non-Windows assertion behavior remains unchanged.
