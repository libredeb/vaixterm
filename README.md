# VaixTerm

![icon](res/vaixterm.png)

VaixTerm is a robust, lightweight, high-performance, terminal emulator for Handheld & Embedded Systems. Built using SDL2, primarily designed for **handheld gaming devices** and embedded systems. It provides a full-featured command-line interface optimized for environments where traditional keyboard input is limited, leveraging game controller input and a highly customizable on-screen keyboard.

![screenshot](docs/imgs/screenshot.png)

## Main Features

VaixTerm combines the power of a standard terminal with features tailored for controller-based use:

- **Full Terminal Functionality:** Enjoy a complete command-line experience with support for ANSI/VT100 escape codes, 256-color and True Color palettes, UTF-8, and a scrollback buffer.
- **Controller-First Navigation:** All terminal functions, including scrolling and special key input, are mapped to a standard game controller.
- **Customizable On-Screen Keyboard:** A powerful OSK allows for text input and execution of complex commands. Its layout is fully configurable, and you can create dynamic sets of keys for your favorite shortcuts.
- **Theming and Customization:** Change the look and feel of your terminal with custom color schemes, fonts, and background images.

## Key Features & Design Principles

*   **Optimized for Handheld & Controller Input:** Engineered with a primary focus on game controller navigation and input, providing a natural and efficient interface for devices without physical keyboards.
*   **Extensible On-Screen Keyboard (OSK):** Features a highly configurable OSK with custom character layouts (`.kb` files) and dynamic key sets (`.keys` files) for shortcuts and internal commands, adapting to diverse workflows.
*   **Lightweight SDL2 Core:** Built on SDL2 for efficient rendering and minimal resource consumption, making it suitable for embedded and resource-constrained environments.
*   **Comprehensive Terminal Emulation:** Supports standard ANSI/VT100 escape codes, 256-color, True Color, and robust UTF-8 character rendering, including custom drawing for box-drawing and Braille characters.
*   **File-Based Configuration:** Appearance and behavior are fully customizable via external `.theme` (color scheme), `.kb` (OSK layout), and `.keys` (key set) files, allowing for easy sharing and management of configurations.

## Getting Started

### Build dependencies

VaixTerm needs a C compiler, `make`, `pkg-config`/`sdl2-config`, SDL2, SDL2_ttf, SDL2_image, and **wget or curl** (to fetch vendored libvterm).

On **Debian 12 Bookworm** (including 64-bit ARM handhelds):

```bash
sudo apt install build-essential pkg-config wget \
  libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
```

### Compile (default: vendored libvterm 0.3.3)

`make` downloads libvterm 0.3.3 into `vendor/libvterm/` if it is missing, then compiles it into the binary. You do **not** need `libvterm-dev`.

```bash
make
```

The build log should say `libvterm=vendored`. Re-fetch from scratch with `make distclean && make`.

### Compile with system libvterm

To link against the distro library instead (Debian 12 ships **0.1.4**; the Makefile probes that older API):

```bash
sudo apt install libvterm-dev
make USE_SYSTEM_VTERM=1
```

The build log should say `libvterm=system`.

Cross-compilation with a Buildroot SDK is supported by setting `BUILDROOT_HOST_DIR` (see the Makefile).

### Install

Default prefix is `/usr`. Override with `PREFIX` if needed. `DESTDIR` is supported for packaging.

```bash
sudo make install
# or: sudo make PREFIX=/usr/local install
```

This installs:

*   `$(PREFIX)/bin/vaixterm`
*   `$(PREFIX)/share/vaixterm/res/` (font, icon, OSK layouts and key sets)
*   `$(PREFIX)/share/applications/vaixterm.desktop`

The `.desktop` file sets `Path` to the data directory so relative resources such as `res/Martian.ttf` resolve, uses `res/vaixterm.png` as the icon, and launches with handheld defaults (`-w 720 -h 720 --osk-grid true --no-credit -s 24`).

```bash
sudo make uninstall
```

### VaixTerm Features

For a full list of command-line features and options, simply run `vaixterm --help`. The output is as follows:

```
vaixterm - A simple, modern terminal emulator for game handhelds.

Usage: ./vaixterm [options]

Options:
  -w, --width <pixels>       Set window width (default: 640)
  -h, --height <pixels>      Set window height (default: 480)
  -f, --font <path>          Set font path (default: res/Martian.ttf)
  -s, --size <points>        Set font size (default: 12)
  -l, --scrollback <lines>   Set scrollback lines (default: 1000)
  -e, --exec <command>       Execute command instead of default shell.
  -b, --background <path>    Set background image (optional).
  -cs, --colorscheme <path>  Set colorscheme (optional).
  --fps <value>              Set framerate cap (default: 30 fps).
  --read-only                Run in read-only mode (input disabled).
  --no-credit                Start shell directly, skip credits.
  --raw                      Raw mode: pass all input directly to child process.
  --log-level <level>        Set log verbosity: debug/info/warn/error/fatal (default: warn).
  --force-full-render        Force a full re-render on every frame.
  --key-set [-|+]<path>      Add key set ('-': available, '+': load).
  --osk-layout <path>        Use a custom OSK layout file.
  --osk-alpha <0-255>        OSK bar transparency (default: 220).
  --osk-height <pixels>      OSK bar height in pixels (default: char height).
  --osk-grid <true|false>    Render OSK as a 2D grid (default: false).
  --version                  Show version and exit.
```

`--osk-grid true` shows the full character layout at once (QWERTY-style rows) and wraps special key sets into columns that fit the screen. The same option can be set in `~/.config/vaixterm/vaixterm.conf` as `osk_grid=true`.
