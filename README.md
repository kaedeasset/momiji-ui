# MomijiUI

A UI framework where screens are written as JSON. The same screen JSON is drawn the same way in Unreal Engine 5 (Slate) and in the browser (canvas). MIT licensed and free to use.

Japanese: [README.ja.md](README.ja.md). **The Japanese version is the original**; this English version is its translation.

- Repository: <https://github.com/kaedeasset/momiji-ui>
- Author: kaedeasset (<https://kaedeasset.com/>)
- Requires Unreal Engine 5.8 and Windows (MSVC 14.51). The browser version is built with Node 18 or later and runs from any static HTTP server (no dependencies)

## What is in this repository

| Location | What it is |
|---|---|
| `Plugins/MomijiUI/` | The UE plugin. Two modules: `MomijiUICore` (loads, lays out and validates screen JSON; engine-agnostic) and `MomijiUISlate` (Slate rendering). Also the fonts, the conformance fixtures (`Fixtures/`), the minimal example (`Plugins/MomijiUI/Examples/counter.json`) and the automated test sources |
| `Tools/momiji-ui/` | The browser version: the source `web/src/momiji-ui.mjs`, `web/build.mjs` that makes the distribution, the screen JSON checker `validate.mjs`, examples in `examples/`, and the conformance tooling (`package.json` and lock) |
| `Scripts/lib/momiji-ui-canvas.js` | The browser version for a classic `<script>` tag (generated from the source by `web/build.mjs`) |
| `Plugins/MomijiUI/Docs/` | Documentation: the setup guide `momiji-ui-guide.en.md` (running the sample, writing screen JSON, using it in UE and the browser, differences, troubleshooting) and the reference `momiji-ui-reference.en.md` (every node type, key, enumeration, animation, theme and diagnostic), in English and Japanese, plus the changelog `momiji-ui-changelog.md`. The same files as `Docs/` in the Fab download |

## Installation

**UE:** copy `Plugins/MomijiUI/` into your project's `Plugins/`, enable MomijiUI in *Edit → Plugins*, and build the project.

**Browser:** build the distribution and serve that folder over static HTTP.
```
node Tools/momiji-ui/web/build.mjs        # → Tools/momiji-ui/web/dist/
node Tools/momiji-ui/examples/serve.mjs   # → open http://localhost:8840/counter/
```

## Minimal usage

The smallest example is a counter that counts up when pressed, `Plugins/MomijiUI/Examples/counter.json`. Your program does four things: load the screen JSON → compute values from your state and draw → change the state on the pressed action → draw again.

- Browser: `Tools/momiji-ui/examples/counter/main.js`
- UE: `Plugins/MomijiUI/Source/MomijiUISlate/Private/Examples/SMomijiUICounterExample.cpp` (to show it in the game window: `GEngine->GameViewport->AddViewportWidgetContent(SNew(SMomijiUICounterExample))`)

To check that a screen JSON follows the rules:
```
node Tools/momiji-ui/validate.mjs Plugins/MomijiUI/Examples/counter.json
```

Details: the setup guide [Plugins/MomijiUI/Docs/momiji-ui-guide.en.md](Plugins/MomijiUI/Docs/momiji-ui-guide.en.md) and the reference with every node type and key [Plugins/MomijiUI/Docs/momiji-ui-reference.en.md](Plugins/MomijiUI/Docs/momiji-ui-reference.en.md) (§1 of the guide is written for a Fab install).

## Tests

- UE automated tests: the sources are `Plugins/MomijiUI/Source/*/Private/*Tests.cpp`. Open your project in the editor, go to *Tools → Session Frontend → Automation*, select `MomijiUI` and run. From the command line: `UnrealEditor-Cmd.exe <your>.uproject -ExecCmds="Automation RunTests MomijiUI; Quit" -unattended -nullrhi`
- UE / browser conformance: in the upstream repository, scripts (`momiji-ui-conformance.ps1`, `momiji-ui-examples.ps1`) draw the `Fixtures/` screens in UE and in the browser and compare them. To compare several browsers, install the tooling with `cd Tools/momiji-ui; npm install; npx playwright install chromium firefox webkit`

## Fab and paid products

- Fab product page: coming soon
- MomijiUI and KaedeVN (the novel engine, <https://github.com/kaedeasset/kaedevn>) are free. kaedeasset builds paid products on top of them; the main one today is **KaedeRPG** (an RPG creation tool for Unreal Engine). See <https://kaedeasset.com/>.

## License

The MomijiUI source is MIT ([LICENSE](LICENSE), © 2026 kaedeasset).

Things that are not MIT or carry their own license:

| What | License | Where |
|---|---|---|
| Fonts BIZ UDPGothic, BIZ UDPMincho (Morisawa) | SIL Open Font License 1.1 | `Plugins/MomijiUI/Resources/Fonts/` (`OFL-BIZ-UDGothic.txt`, `OFL-BIZ-UDMincho.txt`) |
| Fonts Yomogi, DotGothic16 | SIL Open Font License 1.1 | Same folder (`OFL-Yomogi.txt`, `OFL-DotGothic16.txt`) |
| nlohmann/json 3.11.3 | MIT (© Niels Lohmann) | `Plugins/MomijiUI/Source/MomijiUICore/ThirdParty/nlohmann/json.hpp` |

The fonts are redistributed unmodified.

"Unreal" and "Unreal Engine" are trademarks of Epic Games, Inc. MomijiUI is an independent, unofficial plugin and is not affiliated with or endorsed by Epic Games.
