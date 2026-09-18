# FloatKeys — Floating Virtual Keyboard for Windows

Like the Windows touch keyboard, but **with** the missing keys:
`Shift`, `Ctrl`, `Alt`, `Win`, `Tab`, `CapsLock`, `Esc`, `F1–F12`, arrows, `Del`, `PrtSc`, menu key.

Two versions, same behavior:

| | `keyboard.c` → `FloatKeys.exe` (use this) | `floating_keyboard.py` (prototype) |
|---|---|---|
| Size | ~200KB single `.exe` | needs Python |
| RAM | ~10MB | ~40-60MB |
| Runtime | none (built into Windows) | Python 3.10+ |
| Windows | 1 window, owner-drawn GDI | tkinter |

## Run (native, recommended)

Just double-click **`FloatKeys.exe`** — no install, no runtime, no admin.

## Rebuild from source

You only need **Zig** (~50MB, no admin):

```bat
winget install -e --id zig.zig
build-zig.bat
```

Or with MSVC (VS Build Tools, C++ workload, from a Developer Prompt):

```bat
build-msvc.bat
```

## Why this works as a real keyboard

Normal windows steal focus when clicked, so keystrokes would go to the
keyboard itself. This app uses `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` plus
`MA_NOACTIVATE`, so clicks **never activate it** — focus stays in Notepad,
your browser, game, etc. Keys are injected system-wide with `SendInput`.
`WS_EX_LAYERED` gives opacity, `WS_EX_TOPMOST` keeps it floating.

## Modifier behavior (important)

Tap once = **hold for next key only** (light blue).
Tap twice = **lock down** (dark blue). Tap again = release.

Real shortcuts with mouse/touch only:

- `Ctrl` then `C` = Copy, `Ctrl` then `V` = Paste
- `Win` then `L` = Lock PC, `Win` then `E` = Explorer
- `Alt` then `Tab` = switch window (lock `Alt`, tap `Tab`)
- `Shift` + arrows = select text
- `Ctrl` + `Shift` + `Esc` = Task Manager (lock both, tap `Esc`)

`CapsLock` is a real toggle with a live indicator (polls `GetKeyState`).

## Window controls (top bar)

- Drag the bar to move (custom drag, never activates)
- `A+` / `A-` = bigger / smaller, `◐` / `◑` = opacity, `Fn` = show/hide `Esc F1–F12`, `✕` = close
- Closing releases any held/locked modifiers so keys never get "stuck"

Hold `Bksp`, `Del`, `Space`, or arrows to auto-repeat.

## Files

- `keyboard.c` — the whole native app (single file, C99 + Win32 only)
- `keyboard.rc` + `app.ico` (`gen_icon.py` regenerates it) — exe icon, all 7 sizes
- `FloatKeys.exe` — built binary, just run it
- `build-zig.bat` / `build-msvc.bat` — one-line builds
- `floating_keyboard.py` — older Python prototype (same layout/logic)
- `start_keyboard.bat` — launcher for the Python version

## Notes

- US layout. Typing uses `KEYEVENTF_UNICODE` (layout-independent);
  `Ctrl`/`Alt`/`Win` combos use virtual-key codes (`VK_*` + `VK_OEM_*`).
- Sticky-modifier state is tracked in-app; `WM_DESTROY` releases everything.
