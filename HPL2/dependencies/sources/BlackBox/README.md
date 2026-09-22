# BlackBox for Amnesia64

The Windows game links `BlackBox.lib` and calls `BlackBox::Initialize()` before
creating `cLuxBase`, then `BlackBox::Shutdown()` after game cleanup. Both Debug
and Release support Win32 and x64. Non-Windows builds do not use BlackBox.

`Amnesia.sln` builds the library automatically. The game CMake targets also link
it on Windows. The dialog resource is compiled directly into each executable:
putting it only in the static library would allow the linker to discard it.
Resource IDs use a separate range to avoid the game icon's IDs.

The active sources are `BlackBox.cpp`, `BlackBoxUI.cpp`, and `BlackBoxReport.cpp`.
The supplied legacy BugSlayer utilities, DLL binaries, import libraries, old
makefiles, and Visual Studio projects are historical reference only; they are
not used by either current build. The old `ReadMe.txt` describes that DLL build.

The adaptation replaces DLL initialization, 32-bit stack walking and pointer
casts, unbounded report buffers, and the old process-performance registry code.
The UI retains the original compact BlackBox layout, MS Sans Serif font, white
introduction panel, green bug icon, separate Exception Reason/Registers/Stack
Trace sections, and right-hand button column. The header accommodates the
updated contact text and the register panel fits full-width x64 values.
`ClassicBug.ico` was extracted from the supplied legacy DLL's icon resource;
the current game embeds this asset and never loads that DLL.

Information shows the crash-time CPU, Windows version, and memory data. State
shows a bounded crash-time process list and initially selects the game with its
captured modules. Selecting another process queries its current modules, which
may be unavailable if it exited, access is denied, or architectures differ.
The original About and gathering-information dialogs are also restored.

Copy exports the complete Unicode report; Save writes the same report as plain
UTF-8 text, including exception details, registers, up to 128 stack frames,
system information, and the game's modules. The UI sections and export share
the same captured data.
Email and issue buttons open the configured addresses for the user to submit
the report. There is no automatic report upload.

The crash dialog runs on a thread created during initialization, so it has its
own stack even if the game thread overflows. The faulting thread stays alive
while its saved context is inspected. Closing the dialog terminates the crashed
process; execution does not resume. A debugger retains control of exceptions.
The stack list initially has keyboard focus and there is no default Close button.
Enter/Escape carried over from gameplay cannot dismiss it. Close it explicitly
with the Close button or the window's X; keyboard navigation to Close also works.
Keep matching executable PDB files for named frames and source locations;
module-relative addresses remain useful without PDBs. Symbol search uses the
executable directory rather than an inherited network symbol path.

This is an in-process unhandled Windows exception reporter. Fail-fast termination,
forced process termination, and crashes before initialization bypass it. Severe
heap corruption or locks held by another thread can still prevent reporting.
Other components must not concurrently use DbgHelp during crash collection.

## Verification

From a Visual Studio developer terminal with CMake on PATH:

```powershell
cmake -S HPL2/dependencies/sources/BlackBox -B artifacts/blackbox-x64 -G "Visual Studio 18 2026" -A x64 -DBLACKBOX_BUILD_TESTS=ON
cmake --build artifacts/blackbox-x64 --config Release
ctest --test-dir artifacts/blackbox-x64 -C Release --output-on-failure
```

Repeat with `-A Win32` and a separate `artifacts/blackbox-x86` directory, and with
`--config Debug` / `-C Debug`. Tests check handler lifecycle and restoration,
bounded report generation, and real main-thread access violations, worker-thread
access violations, and stack overflows in isolated child processes. They inspect
the crash dialogs and verify the exception, registers, caller frames, contact
details, and process termination. Held Enter/Escape regression checks verify that
gameplay input cannot dismiss the report; deliberate button, keyboard, and window
close actions still work. They also open Information, State, and About and check
that the data panels are populated. Run outside a debugger. For optional visual
QA, set `BLACKBOX_CAPTURE_DIR` to an existing directory before running the tests;
they save BMP captures of their own child dialogs there.

For manual export checks, run `BlackBoxTests.exe access` to leave a crash dialog
open. Check Copy by pasting into a text editor, Save to a path with Unicode
characters, cancel Save, and try an unwritable destination. Check Email and
Submit issue open the displayed destinations. These actions are deliberately
not exercised by the automated suite, which leaves the clipboard and external
applications alone.
