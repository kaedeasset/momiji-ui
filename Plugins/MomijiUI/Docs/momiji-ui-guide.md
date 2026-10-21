# MomijiUI Setup Guide

This guide is for MomijiUI 0.1.0 (2026-10-21).

MomijiUI is a free UI framework: you write screens as JSON, and Unreal Engine (UE) and the browser draw them with the same layout and the same controls. This guide takes you from installing it from Fab to running the sample counter in UE, writing a screen of your own and checking it.

![The same settings screen JSON drawn by UE (Slate) on the left and by the browser (canvas) on the right](images/momiji-ui/same-ue-web.png)

- Every node type, key, enumerated value, animation, theme and diagnostic code is in the [reference](momiji-ui-reference.md).
- What changed in each version is in the [changelog](momiji-ui-changelog.md).
- [日本語](momiji-ui-guide.ja.md)

---

## Contents

1. [Your first run (from install to checking a screen)](#1-your-first-run-from-install-to-checking-a-screen)
2. [What this plugin does](#2-what-this-plugin-does)
3. [Requirements](#3-requirements)
4. [Your program's four jobs](#4-your-programs-four-jobs)
5. [Using it in UE](#5-using-it-in-ue)
6. [Using it in the browser](#6-using-it-in-the-browser)
7. [Differences between UE and the browser](#7-differences-between-ue-and-the-browser)
8. [What it cannot do yet](#8-what-it-cannot-do-yet)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Your first run (from install to checking a screen)

These are the steps for someone using MomijiUI in UE for the first time, from installing it from Fab to writing one screen of your own and checking it. Follow them in order and you reach the end without reading any other chapter. Read the other chapters later, when you build MomijiUI into your own game.

**First, the overall order.** MomijiUI is not an application you launch on its own. You add it to your UE project and use it from C++. There are no editor menus and no Blueprint nodes. In this guide, "launch" means starting a program (the launcher, UE), and "open" or "show" means bringing up a window or a screen inside a running program.

| Step | What you do | Where | When it is done |
|---|---|---|---|
| 1.1 | Install MomijiUI from Fab | Fab and the launcher | MomijiUI is installed in UE |
| 1.2 | Install UE 5.8 and Visual Studio | The launcher and Microsoft's installer | You are ready to create a C++ project |
| 1.3 | Create a C++ project and enable MomijiUI | UE's project browser and the editor | A project with MomijiUI opens |
| 1.4 | Run the sample counter in UE | The UE editor (Play) | A screen whose count goes up when you press appears |
| 1.5 | See the same JSON run in the browser | A browser | The same screen works in the browser too |
| 1.6 | Write a screen of your own and show it | A text editor and UE | You get a picture of your screen |
| 1.7 | Check the screen JSON | Visual Studio and the UE editor | Mistakes are reported with their location |

From the second time on, just open the project you created in 1.3. Steps 1.1 to 1.3 are needed only once.

### 1.1 Install MomijiUI from Fab

1. Sign in to Fab (fab.com) with your Epic Games account and choose "Add to My Library" on the MomijiUI page. It is free.
2. In the Epic Games Launcher, MomijiUI appears in the Fab section of **Unreal Engine → Library**. Press **Install to Engine** and choose **5.8**. The plugin goes into the MomijiUI folder (the folder with `MomijiUI.uplugin`) under `<UE folder>\Engine\Plugins\Marketplace\`. This guide calls that folder "the plugin folder".

If UE 5.8 is not installed yet, do step 1 of 1.2 first and come back.

### 1.2 Install UE 5.8 and Visual Studio

1. In the launcher, under **Unreal Engine → Library**, press **+** next to the engine versions, choose **5.8** and press **Install**.
2. Download the **Visual Studio Community** installer (free) from Microsoft, run it, tick the **Game development with C++** workload and install. Use **the latest Visual Studio 2022 (17.14) or the latest Visual Studio 2026** (UE 5.8 does not build with some older versions — 2022 17.13 or earlier and the first 2026 releases. If you already have it, press **Update** in the Visual Studio Installer). MomijiUI is used from C++, so the project is a C++ project (1.3), and that needs Visual Studio.

### 1.3 Create a C++ project and enable MomijiUI

1. In the launcher, press **Launch** for 5.8.
2. In the project browser, choose **Games → Blank**, then **C++**. Name the project with **letters, digits and `_` only** (for example `MyUi`) and press **Create**. Visual Studio builds the project's code and the editor opens.
3. In the editor, open **Edit → Plugins**, search for **MomijiUI**, and tick it to enable it. MomijiUI is currently a beta, so a beta warning appears; press **Yes** to continue.
4. When asked to restart, restart the editor with the same project.
5. Close the editor (in 1.4 you launch it again with an argument).

Installing a plugin into the engine does not make it usable; you enable it per project. You do this only once, right after creating the project.

### 1.4 Run the sample counter in UE

The sample is a small screen with a press count and a list of names. Its layout is `Examples/counter.json` in the plugin folder, and the code that drives it is the sample widget included in the plugin (`SMomijiUICounterExample`). Launch the editor with the argument `-KaedeUiExample=counter`, and the screen appears over the game view when you start playing.

1. Open PowerShell and type this line (change the UE and project locations to yours. The UE location is the install location shown in the launcher under **Unreal Engine → Library**, **▼ → Options** next to **Launch** for 5.8; use `Engine\Binaries\Win64\UnrealEditor.exe` inside it):
   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MyUi\MyUi.uproject" -KaedeUiExample=counter
   ```
2. When the editor opens, press **▶ (Play)** in the toolbar. The sample counter appears over the game view.
3. Press **+1** and the count goes up. From one press on, **リセット** (Reset) can be pressed, and at five presses "5 回押した" (pressed five times) appears. Type a name in the field below and press Enter (or **追加**, Add) to add it to the list. Japanese input (IME) works too. The sample's labels are Japanese because the sample is the same JSON the Japanese guide uses.
4. If you click outside the nodes and the mouse pointer disappears, press **Shift+F1** to get it back (the game took the mouse). Press **Esc** to stop playing.

The sample appears only on the first Play of that launch. To see it again, close the editor and start over from step 1.

### 1.5 See the same JSON run in the browser

MomijiUI has a browser version that draws the same screen JSON on an HTML `<canvas>`.

1. Open the top page of the documentation site, <https://momiji.kaedeasset.com/>, in a browser.
2. The page has a running counter drawn from the same `counter.json` as in 1.4. Check that **+1**, **リセット** and the name field are in the same places and behave the same way as in 1.4.

![The sample counter in the browser, with a count of 3 and three names](images/momiji-ui/counter.png)

The browser distribution (the files you load from your own HTML) is on GitHub Releases (§6).

### 1.6 Write a screen of your own and show it

Write a screen with one text (`Label`) and one button (`Button`), and take a picture of it in UE.

1. In the project folder (the folder with `MyUi.uproject`), create a folder named `UI`, and in it create `hello.json` with this content. Save it as UTF-8.
   ```json
   {
    "bind": { "strings": { "name": "MomijiUI" } },
    "root": {
     "type": "VerticalLayout", "anchor": "center", "w": 400, "h": 200, "pad": 20, "gap": 12,
     "children": [
      { "type": "Label", "text": "Hello {name}", "h": 32, "fontSize": 20, "align": "center" },
      { "type": "Button", "text": "OK", "action": "ok", "h": 44 }
     ]
    }
   }
   ```
   - The screen is what is inside `root`. The `type` decides the node, and `children` nests nodes.
   - Screens are written as 1280×720. `anchor: "center"` puts the box in the middle.
   - `{name}` is replaced with a value passed when drawing. Here it is drawn with the value written in `bind` (`MomijiUI`).
   - `action` is the string sent to your program when the button is pressed.
2. Close the editor and type this line in PowerShell:
   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MyUi\MyUi.uproject" -game -ResX=1280 -ResY=720 -KaedeUiShot=UI/hello.json
   ```
   A game window opens, draws your screen, takes one picture, and closes. The first launch of a new project can take about a minute before the window appears (shaders are prepared); wait until the window closes.
3. Open `Saved\Screenshots\WindowsEditor\MomijiUI.png` in the project folder. If the box in the middle shows "Hello MomijiUI" and an **OK** button, your screen works.

The location given to `-KaedeUiShot` is relative to the project folder, or absolute.

### 1.7 Check the screen JSON

Mistakes (an unknown node type, a misspelled key, a wrong value type, a duplicate id) are found before drawing by MomijiUI's check (diagnostics). The check is the plugin function `KaedeCore::ValidateScreenJson`. Add one file to your project so you can call it from the editor's command field.

1. In Visual Studio, open `MyUi.sln`, created in 1.3 in the project folder.
2. In `<project folder>\Source\MyUi\MyUi.Build.cs`, add `"MomijiUICore"` to the `PublicDependencyModuleNames` line:
   ```csharp
   PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "MomijiUICore" });
   ```
   (Keep the names already inside `{ … }` in your file and add `"MomijiUICore"` at the end.)
3. In the same folder (`<project folder>\Source\MyUi\`), create `MomijiCheck.cpp` with File Explorer or Notepad, paste this content and save it as **UTF-8** (Visual Studio's "Add New Item" may default to a folder under `Intermediate`, and a file there is not built):
   ```cpp
   #include "CoreMinimal.h"
   #include "HAL/IConsoleManager.h"
   #include "Misc/FileHelper.h"
   #include "Misc/Paths.h"
   #include "MomijiUICore.h"

   // Type "Momiji.Check UI/hello.json" in the editor's command field to print the screen JSON's diagnostics to the Output Log
   static FAutoConsoleCommand GMomijiCheck(
   	TEXT("Momiji.Check"),
   	TEXT("Momiji.Check <screen JSON, relative to the project folder>"),
   	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
   	{
   		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("Momiji.Check <screen JSON>")); return; }
   		FString Json;
   		if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectDir() / Args[0])))
   		{
   			UE_LOG(LogTemp, Error, TEXT("Cannot read: %s"), *Args[0]);
   			return;
   		}
   		const auto Diags = KaedeCore::ValidateScreenJson(TCHAR_TO_UTF8(*Json));
   		for (const kaedevn::ui::UiDiagnostic& D : Diags)
   		{
   			UE_LOG(LogTemp, Warning, TEXT("%s %s %s %s"), UTF8_TO_TCHAR(D.severity.c_str()), UTF8_TO_TCHAR(D.code.c_str()),
   				UTF8_TO_TCHAR(D.path.c_str()), UTF8_TO_TCHAR(D.message.c_str()));
   		}
   		UE_LOG(LogTemp, Display, TEXT("Momiji.Check: %d diagnostics for %s"), (int32)Diags.size(), *Args[0]);
   	}));
   ```
4. In Visual Studio's toolbar, make sure the configuration is **Development Editor** and **Win64**, then choose **Build → Build Solution**. When it ends with "1 succeeded", the build is done.
5. Open `MyUi.uproject` to launch the editor, and open **Window → Output Log**. In the field at the bottom (**Cmd**), type `Momiji.Check UI/hello.json` and press Enter. If the Output Log shows "Momiji.Check: 0 diagnostics for UI/hello.json", there are no mistakes.
6. Now see a mistake being found. Change `"Label"` to `"Labl"` in `hello.json`, save, and type `Momiji.Check UI/hello.json` again. One line `error unknown-type /root/children/0/type …` appears (severity, code, location, message; the message is in Japanese). Change it back to `"Label"` and save, and the count returns to 0.

![What the check returns: a screen JSON with five mistakes on the left and its diagnostics (severity, code, location, message) on the right, drawn from the browser's check](images/momiji-ui/diagnostics.png)

That is the end of your first run. When you build MomijiUI into your own game, read §4 and §5. Where to look up every node type and key is listed at the top of this guide.

---

## 2. What this plugin does

- **Screens are JSON.** Nodes, sizes, colors, font sizes, ordering and conditions go in JSON. Your program passes values (text, numbers, lists) and receives the actions that were pressed. Changing a screen's layout does not require rebuilding code.
- **The same layout and controls in UE and the browser.** From the same screen JSON and the same values, UE (Slate) and the browser (`<canvas>`) produce the same positions, the same clickable areas and the same focus movement. The 19 conformance fixtures included in the plugin (`Fixtures/`) are used to compare the two ([reference](momiji-ui-reference.md) §11).
- **17 node types.** Panel (`Panel`), image (`Image`), text (`Label`), button (`Button`), slider (`Slider`), toggles (`Toggle`, `ToggleGroup`), scrolling (`ScrollView`), modal (`Modal`), vertical / horizontal / grid containers (`VerticalLayout`, `HorizontalLayout`, `GridLayout`), a one-line text field (`TextInput`), polyline (`Polyline`), ellipse (`Ellipse`), polygon (`Polygon`) and divider (`Divider`).
- **Layout.** Stack vertically or horizontally, share the rest with `flex`, grids, scrolling, and `anchor` (7 values) to stick to an edge or the center. Heights you do not write are decided by the content and the number of wrapped lines.
- **Values.** You pass text (`strings`), numbers (`vars`) and lists (`lists`) each time you draw. `{key}` replacement, `repeat` with `template` to lay out a list (`templates` to change the shape per item kind), and conditions (`showIf`, `hideIf`, `enabledIf`, `highlightIf`).
- **Focus and input.** Press nodes with the mouse, the wheel and the scroll-bar thumb, and with the arrow keys or a gamepad move focus from node to node and press (where focus goes is worked out from the nodes' positions). Opening a `Modal` moves focus inside it, and closing it brings focus back. In the browser, `attach` takes the arrow keys and Enter / Space. Keys and gamepads in UE, and gamepads in the browser, are received by your program, which calls the renderer's focus move (`MoveFocus` in UE, `R.moveFocus` in the browser) (§5.2).
- **Text fields and IME.** `TextInput` supports Japanese input (IME). Its content is read and written by its `bindVar` name.
- **Text selection and copy.** Write `selectable` on a container (`Panel`, `VerticalLayout` or `ScrollView`), and players can drag over the text in it to select it and copy it to the clipboard.
- **Animation.** Nine `anim` keys, including how a node first appears (`fade`, `slide`, `pop`), counting numbers up, pulsing, and delaying items to show a list in order.
- **Themes.** Three built in (`atelier`, `midnight`, `sakura`). A theme JSON changes colors and shapes. In UE, visual keys such as window shapes, edges, shadows, gradients, and text outlines and glows are also drawn (§7).
- **Checking.** Before drawing, the diagnostics report unknown node types, misspelled keys, wrong value types, values outside an enumeration, duplicate ids, named colors and more, with their location in the JSON (8 codes, [reference](momiji-ui-reference.md) §10).
- **Bundled fonts.** BIZ UDPGothic and BIZ UDPMincho (Regular and Bold), the handwriting-style Yomogi and the pixel font DotGothic16 (all SIL OFL 1.1). Because it does not rely on OS fonts, Japanese text shows even on English Windows.
- **The browser version has no dependencies.** One ES module (plus a classic form loaded with `<script>`). Get it from GitHub Releases (§6).
- **Where it is used.** The game screens of kaedeasset's KaedeRPG (an RPG creation tool) and KaedeVN (a visual novel engine) are drawn with MomijiUI.

| The 17 node types (UE) | Japanese typed into a text field (UE) |
|---|---|
| ![All 17 node types on one screen](images/momiji-ui/components.png) | ![A text field with かえで typed in](images/momiji-ui/text-input.png) |

| atelier theme | midnight theme | sakura theme |
|---|---|---|
| ![A settings screen in the built-in atelier theme](images/momiji-ui/theme-atelier.png) | ![A settings screen in the built-in midnight theme](images/momiji-ui/theme-midnight.png) | ![A settings screen in the built-in sakura theme](images/momiji-ui/theme-sakura.png) |

| KaedeRPG menus (four themes) | KaedeVN config |
|---|---|
| ![The KaedeRPG menu in four themes](images/momiji-ui/kaederpg-menus.png) | ![The KaedeVN config screen](images/momiji-ui/kaedevn-config.png) |

## 3. Requirements

| Item | Version / condition |
|---|---|
| Unreal Engine | 5.8 |
| OS | Windows (only Windows has been checked) |
| Visual Studio | The latest 2022 (17.14) or the latest 2026, with the "Game development with C++" workload |
| Project | A C++ project (MomijiUI is used from C++; there are no Blueprint nodes) |
| For the browser version | A static HTTP server (with `file://`, fonts and `fetch` do not work). Checked with Chrome on Windows (and Chromium, Firefox, WebKit) |

## 4. Your program's four jobs

In UE and in the browser alike, your program does the same four things:

1. **Read the screen JSON** (once).
2. **Make values from your state and draw.** Pass text, numbers and lists.
3. **Change your state from the pressed action (`action`).** You decide what each `action` means.
4. **Draw again.** Back to 2.

Input (pressing, the wheel, scrolling, dragging sliders, editing text fields, Japanese input) is handled by MomijiUI. Only events reach your program; you never interpret coordinates or hit areas yourself. Arrow keys and Enter are handled by `attach` in the browser; in UE your program passes them to the canvas's `MoveFocus` and `FocusedAction` (§5.2).

| Job | UE | Browser |
|---|---|---|
| 1 Read | `KaedeCore::LoadScreenTree(<JSON string>)` | `MomijiUICanvas.load(json)` |
| 2 Draw | `KaedeUI::PresentScreen(Canvas, Tree, Values, Theme, Bg)` | `MomijiUICanvas.present(R, tree, values, bg)` |
| 3 Receive | `Canvas->SetOnAction(…)` | `MomijiUICanvas.attach(R, canvas, { onEvent, render, … })` |
| 4 Draw again | Call `PresentScreen` again | Call `present` (`render`) again |

In the browser, `onEvent` receives typed events (press, Enter in a text field, slider, toggle) with their values. In UE, `SetOnAction` receives only the `action` string and the slider position; read text-field contents with `GetTextInputValue`. The event shapes are in §9 of the [reference](momiji-ui-reference.md).

## 5. Using it in UE

### 5.1 Setup

In your module's `*.Build.cs`, depend on `MomijiUICore` and `MomijiUISlate`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "MomijiUICore", "MomijiUISlate" });
```

`MomijiUISlate` stages the fonts with your packaged game (nothing for you to do).

### 5.2 Writing a widget

The sample `Source/MomijiUISlate/Private/Examples/SMomijiUICounterExample.cpp` (in the plugin folder) is the smallest widget that does the four jobs. The key parts:

```cpp
#include "SKaedeUiCanvas.h"
#include "MomijiUICore.h"

// 1. Read the screen JSON (once)
FString Json;
FFileHelper::LoadFileToString(Json, *Path);
Tree = KaedeCore::LoadScreenTree(TCHAR_TO_UTF8(*Json));
Theme = KaedeCore::BuiltinTheme("midnight");

// Place the canvas and receive pressed actions
ChildSlot[ SAssignNew(Canvas, SKaedeUiCanvas) ];
Canvas->SetOnAction([this](const FString& Action, float /*RelX*/) { HandleAction(Action); });

// 2. Make values from your state and draw (after changing the state in 3, call this again in 4)
KaedeUI::FKaedeScreenValues V;
V.Strings["title"] = "…";
V.Vars["count"] = Count;
KaedeUI::PresentScreen(*Canvas, Tree, V, Theme, FLinearColor::FromSRGBColor(FColor(0x0e, 0x11, 0x16)));
```

- To show it in the game window, use `GEngine->GameViewport->AddViewportWidgetContent(SNew(YourWidget))`.
- To let players click your UI, show the mouse pointer and pass input to the UI in your player controller (`bShowMouseCursor = true` and `SetInputMode(FInputModeGameAndUI())`). The default game of a blank project takes the mouse and hides the pointer.
- The canvas handles mouse presses, the wheel, the scroll-bar thumb and slider drags. **The canvas does not take keyboard or gamepad input by itself.** Receive it in your program (Enhanced Input or similar): for a direction, call `Canvas->MoveFocus(dx, dy)` (dx and dy are -1, 0 or +1) and `Canvas->SetFocusVisible(true)`; to confirm, get the focused node's `action` with `Canvas->FocusedAction()` and handle it as if it had been pressed.
- `SKaedeUiCanvas` overlays its own edit field on text fields (with Japanese input). Read the content with `Canvas->GetTextInputValue("<bindVar>")` and set it with `SetTextInputValue`.
- Pass a screen name as the last argument of `PresentScreen` to keep text-field contents, scroll positions and focus per screen. Discard them with `ResetScreenState(<screen name>)`.
- For screens with animation (`anim`), keep a `kaedevn::ui::UiMotionState` in your widget, pass it to `PresentScreen`, and call `update(<milliseconds>)` in Tick.
- Choose a theme with `KaedeCore::BuiltinTheme("<name>")` or `KaedeCore::LoadTheme(<theme JSON string>)`, and a typeface with `Canvas->SetFontFamily(…)`.

The full list of calls is in §9.2 of the [reference](momiji-ui-reference.md).

### 5.3 Looking at a screen only

To check how a screen JSON looks before writing code that builds values, use the launch argument from 1.6, `-KaedeUiShot=<screen JSON>`. It takes one picture and quits, writing `Saved\Screenshots\WindowsEditor\MomijiUI.png`. If the JSON has `"bind": { "strings": {…}, "vars": {…}, "lists": {…} }`, it draws with those values (`bind` is not read when drawing; it exists only for this picture and for the conformance fixtures).

### 5.4 When to call the check

`KaedeCore::ValidateScreenJson` is a checking tool and is not called when drawing. Call it during development as in 1.7, or in an automated test that runs every screen JSON of yours through it and expects 0 errors. Reading (`LoadScreenTree`) keeps going when a key has the wrong type, using the default for that key only (it does not stop).

## 6. Using it in the browser

The browser distribution (the files you load from your own HTML) is `momiji-ui-web-<version>.zip` on GitHub Releases (<https://github.com/kaedeasset/momiji-ui/releases>). Unzip it and put the files on your site. To build it from source, run `node Tools/momiji-ui/web/build.mjs` in the repository (<https://github.com/kaedeasset/momiji-ui>); it is written to `Tools/momiji-ui/web/dist/`. You can also see it run in the sample on the documentation site's top page (<https://momiji.kaedeasset.com/>). The distribution contains:

| File | What |
|---|---|
| `momiji-ui.mjs` | The library (an ES module with no dependencies) |
| `momiji-ui-canvas.js` | The classic form loaded with `<script src>` (`window.MomijiUICanvas`), generated from the same source |
| `momiji-ui.css` | The standard style (the 1280×720 stage, the canvas, the text fields' `<input>`, and loading the bundled fonts) |
| `fonts/` | BIZ UDPGothic (Regular and Bold) and its license |
| `examples/counter/` | The sample from 1.5 (its screen JSON is the same file as the UE sample) |

How it is used (the same four jobs as in UE):

```js
import { MomijiUICanvas } from './momiji-ui.mjs';
const canvas = document.getElementById('screen');
const R = new MomijiUICanvas.Renderer(canvas);
R.setTheme('midnight');
const { root: tree } = MomijiUICanvas.load(await (await fetch('counter.json')).json());   // 1. read (stops on an error)
await MomijiUICanvas.loadFonts();
const render = () => MomijiUICanvas.present(R, tree, values(), '#0e1116');                // 2. draw
const host = MomijiUICanvas.attach(R, canvas, {
  stage: document.getElementById('stage'), render, fit: true,
  onEvent(ev) { /* 3. change the state from ev.action */ render(); },                       // 4. draw again
});
```

- Put the `<canvas>` inside a 1280×720 stage (`class="momiji-ui-stage"`) and open the page from a static HTTP server.
- A canvas cannot take Japanese input, so `attach` overlays an `<input>` on each text field. Read the content with `host.inputValue('<bindVar>')` and set it with `host.setInputValue`.
- When you are done, call `host.dispose()`.
- Themes draw only the 11 color keys (the shape keys are UE only, §7). The only typeface is Gothic.

The full list of calls is in §9.3 of the [reference](momiji-ui-reference.md).

## 7. Differences between UE and the browser

| What | UE | Browser |
|---|---|---|
| Layout, clickable areas, focus movement, scrolling, number of wrapped lines, check results | Same | Same |
| How text and corners are painted (smoothness, weight) | Slate's painting | Canvas painting (text looks slightly heavier) |
| Visual keys (window shapes, edges, shadows, gradients, text outlines and glows, letter spacing, …) | Drawn | Accepted by the check as known keys, but not drawn |
| Theme shape and decoration keys (18) | Drawn | Only the 11 color keys are drawn |
| `Polygon` and `Divider` nodes | Drawn | Drawn as a rectangle in the panel color |
| Animations `blink`, `bob`, `frames`, `exit`, `tween` | Run | Do not run |
| `"open"` for `anim.enter.type` | Drawn (but the check reports it as a `bad-value` error) | Stopped by the check |
| Typefaces | Gothic, Mincho, handwriting, pixel | Gothic only |

Which keys are "UE only" is shown in the "Browser" column of the [reference](momiji-ui-reference.md) tables. For screens that must look the same in UE and the browser, do not use "UE only" keys.

## 8. What it cannot do yet

- **It cannot be used from Blueprint or UMG.** Use it from C++.
- **The browser version is not on npm.** Get it as a zip from GitHub Releases (§6).
- **The browser does not draw visual keys or theme shapes yet** (§7).
- **Screen readers are not supported.**
- **UE has been checked on Windows only.** UE on Mac or Linux, iOS and Android have not been checked. In the browser, release Safari (macOS), Edge, and browsers on Mac and Linux have not been checked.
- **There is no in-editor tool for building screen JSON.** Write screen JSON in a text editor and check it with `-KaedeUiShot` (§5.3) and the check (1.7).

## 9. Troubleshooting

| What happened | Cause and fix |
|---|---|
| In 1.4 nothing appears after pressing ▶ | The editor was not launched with `-KaedeUiExample=counter`. It appears only on the first Play of that launch, so after it has appeared once, close the editor and launch it again. If it still does not appear, look for `[ui-example]` lines in the Output Log (for example, the sample screen could not be read) |
| Pressing buttons in the sample or your UI does nothing, or the mouse pointer disappears | The game has taken the mouse. While playing, press **Shift+F1**. In your own game, show the pointer in the player controller as in §5.2 |
| In 1.6 no picture is taken, or the window closes at once | The `-KaedeUiShot` location is wrong, or the file is not valid JSON. Look for `[ui-shot]` lines in the log (`Saved\Logs\MyUi.log`) |
| In 1.7 `Momiji.Check` is an unknown command | The project was not built, or `MomijiCheck.cpp` was added to a different module. Close the editor and build again in Visual Studio |
| The build cannot find `MomijiUICore.h` | `"MomijiUICore"` was not added to `*.Build.cs` (step 2 of 1.7) |
| Children do not appear, or have size 0 | `Panel` does not lay out its children (their `x` `y` `w` `h` are used as they are). Use `VerticalLayout` to stack them |
| `{name}` is shown as is | The value was not passed (unknown keys are left as they are). Check the key's spelling |
| A condition has no effect | Names in conditions must be passed as numbers (`vars`). Text (`strings`) cannot be used in conditions |
| A color does not change | Colors are `#rrggbb` or `#rrggbbaa` only (`red` and the like are ignored; the check reports `bad-color`) |
| Text is larger than expected | `fontSize` is in points (×4/3 px on screen) |
| An animation (`anim`) does not run | The animated node has no `id` or `action` |
| The node chosen with the arrow keys jumps after redrawing | Several buttons share an `action` and have no `id`. Give stateful nodes an `id` (inside a list, `"id": "row:{item.id}"`) |
| Wrapping is off in the browser | It draws before the fonts are loaded. Wait for `await MomijiUICanvas.loadFonts()` before drawing |
| Japanese cannot be typed in the browser | No `<input>` is overlaid on the text field. Use `attach` |
