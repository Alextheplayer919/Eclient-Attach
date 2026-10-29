# E-Client ClickGUI (rebranded from Apollon extraction)

Source: `clickgui.zip` → `mc-src/Apollon_Client_ImGui/app/src/main/jni/` → rebranded to **E-Client**.

| File | What it is |
|---|---|
| `menu.h` | The clickgui: `BeginDraw()` (main window, left SidePanel, 8 pages via `pageId`), `TickFunctions()` — **rebranded**: title is `E-Client`, original author text and external invite links removed |
| `dark_theme.h` | `embraceTheDarkness()` – purple/black ImGui style |
| `menu_utils.h` | UI helpers: `BackGroundDots`, `AddNotification`/`RenderNotifications`, `ImGuiColoredText`, `CreateTextureFromMemoryPNG`, `DrawSeparatorToWindowEdge`, res scaling (+ ESP helpers) |
| `variables.h` | Globals the widgets bind to (checkboxes/sliders) |
| `hotkeys.h` / `RGB.h` | Hotkey panel + RGB helper |
| `Images/` | Embedded PNG icon headers used by menu.h / menu_utils.h |
| `_main_init_excerpt.txt` | How original wired it up (theme init, per-frame `BeginDraw(); RenderNotifications();`) |

Pages (side panel): Movement, Combat, Building, List, Visuals, Hotkeys, Other, Info.
Rebrand: `Apollon_Client v4.62` → `E-Client`, config path `Apollon.json` → `EClient.json`, all original author branding and external invite links removed.
Needs Dear ImGui (with Android + GLES3 backends). `OBFUSCATE` is now identity macro in E-Client.
