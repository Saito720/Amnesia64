# Enemy_Llama decision protocol tests

This standalone test compiles the game's actual strict decision parser without
initializing the engine or loading any model. It covers field types, all enum
values and numeric endpoints, missing/unknown/duplicate fields (including
escaped duplicate keys), malformed/trailing JSON, non-finite/out-of-range
numbers, integer image coordinates, Unicode memory bounds, and preservation of
the prior decision after rejected output.

Approach fixtures verify that the parser and optional native grammar preserve
left/right turn signs, zero forward movement while turning, and forward
movement toward a centered target. They are independent of the system prompt's
wording and do not prove that a model selects the appropriate action. The
autonomous prompt uses a compact description of the magenta target and asks
for alignment before advancing, without complete JSON examples that can be
copied instead of interpreting the image. Runtime tests separately exercise
the actual mover's camera-relative turn goals, expiry and measured progress.

The generation grammar emits `target_x,target_y` before the behavior and motor
fields, so the model can locate the current image target before committing to
an action. The ten-field protocol and validation bounds are unchanged. Parser
checks accept the prior behavior-first order as well as arbitrary field order;
grammar checks verify the new generation order. This is a prompting experiment,
not evidence that field order alone ensures visual grounding.

The separate small steering protocol accepts exactly `target_visible`,
`target_side` and `action`. Parser checks cover both turn directions, centered
forward movement, absent-target wait, field order, every required type and enum,
missing/extra/duplicate keys, malformed UTF-8 and trailing content. Invalid
replies preserve the prior decision. Grammar checks cover the same bounded
vocabulary. Contradictory visibility/side/action combinations are deliberately
accepted and preserved by both: the parser and grammar must not disguise a
model error by automatically selecting the appropriate turn. The compact
steering prompt requests alignment before advancing and contains no human,
combat-state, history or complete JSON-example semantics. Native runtime tests
exercise the controller's mapping of the selected action into bounded movement.

The shared explanation prompt tests also verify that the actual original
system/user input, context summary, raw command reply and engine application
status survive JSON quoting, including Unicode, quotes, newlines and apparent
prompt headings. Missing evidence stays empty. The diagnostic requests brief
prose and labels its explanation as retrospective, with incomplete historical
context; it cannot establish the original inference's internal reason. It asks
about fictional combat restrictions without assuming that a refusal occurred.
These contract tests validate prompt construction rather than model honesty or
explanation quality. Runtime tests separately verify that diagnostic replies
cannot execute enemy actions.

From the repository root:

```powershell
cmake -S HPL2/tests/llama-decision -B bld/llama-decision-tests -G "Visual Studio 18 2026" -A x64
cmake --build bld/llama-decision-tests --config Release
ctest --test-dir bld/llama-decision-tests -C Release --output-on-failure
```

To also initialize and exercise the exact GBNF grammar using the pinned
llama.cpp parser, configure with an existing matching llama-enabled engine
archive:

```powershell
cmake -S HPL2/tests/llama-decision -B bld/llama-decision-tests-native -G "Visual Studio 18 2026" -A x64 -DHPL2_PREBUILT_LIBRARY=D:/Amnesia64/x64/Release/HPL2.lib
cmake --build bld/llama-decision-tests-native --config Release
ctest --test-dir bld/llama-decision-tests-native -C Release --output-on-failure
```

The native cases consume ASCII decisions character by character through
llama.cpp's grammar states. They verify accepted complete decisions and reject
invalid actions/ranges, duplicate fields, trailing output, oversized memory and
incomplete prefixes. Already deployed CUDA runtime DLLs are copied beside the
consumer when present. These tests require no model weights or downloads and
do not establish enemy behavior quality.
