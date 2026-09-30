# Visual test

Runs the launcher on placeholder content and saves screenshots of its main
screens, to look at a change (or compare two builds) without real games.

```
cmake -S . -B build && cmake --build build
tools/visual-test/run.sh build/emerald_launcher visual-test-out
```

What it does:

1. `testcore.c` is compiled: a tiny libretro core that draws a moving
   gradient, supports save states, reset and controller ports, and marks the
   Ancient Stone Tablets week as complete after two seconds.
2. `make_sandbox.py` builds a throwaway folder from the repository's
   `db.json`: an empty file for every ROM, placeholder logos, screenshots and
   box covers, and the test core under every core name the database uses.
3. `shoot.sh` starts the launcher in a virtual display (Xvfb, software
   OpenGL), presses keys and takes screenshots. `run.sh` lists the scenarios:
   the shelf and a version stack, the versions view, the back of a box,
   Settings, Controls, a game with the pause menu and a saved state, the
   Spanish UI and the classic list.

Requirements (Linux): a C compiler, Python 3 with Pillow, `Xvfb`, `xdotool`
and ImageMagick. On Debian/Ubuntu:

```
sudo apt install xvfb xdotool imagemagick python3-pil
```

`shoot.sh` can also be used on its own for a custom sequence:

```
tools/visual-test/shoot.sh build/emerald_launcher /path/to/sandbox out/test \
    wait:5 snap:shelf key:Return wait:1.5 snap:versions
```
