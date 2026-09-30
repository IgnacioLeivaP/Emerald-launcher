# Nintendo Switch Homebrew — Notas técnicas del proyecto VB Wario Player

Este documento resume lo aprendido construyendo un emulador de Virtual Boy para Switch
como homebrew NRO. Muchas de estas lecciones aplican directamente a cualquier proyecto
Switch con SDL2 + OpenGL ES.

---

## Stack técnico

- **Toolchain**: devkitPro / devkitA64
- **Librería del sistema**: libnx
- **Ventana / input / audio**: SDL2 (portlib de devkitPro)
- **Gráficos**: OpenGL ES 3.0 (GLES3)
- **Empaquetado**: NRO + NACP (herramientas de libnx)

---

## Build system (CMake)

```cmake
cmake_minimum_required(VERSION 3.20)
project(mi_app C)   # devkitPro setea CMAKE_SYSTEM_NAME DENTRO de project()

# El check DEBE ir después de project()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    message(FATAL_ERROR "Solo compila para Switch")
endif()

list(APPEND CMAKE_PREFIX_PATH "$ENV{DEVKITPRO}/portlibs/switch")
list(APPEND CMAKE_PREFIX_PATH "$ENV{DEVKITPRO}/libnx")

find_package(SDL2 REQUIRED)

add_executable(mi_app src/main.c ...)

# SDL2 en devkitPro no propaga los include dirs automáticamente — hay que agregarlos a mano
target_include_directories(mi_app PRIVATE
    $ENV{DEVKITPRO}/portlibs/switch/include
    $ENV{DEVKITPRO}/libnx/include)

target_compile_definitions(mi_app PRIVATE NINTENDO_SWITCH=1)

target_link_libraries(mi_app PRIVATE
    SDL2::SDL2main SDL2::SDL2
    EGL GLESv2 glapi drm_nouveau nx)

# Empaquetar como NRO
nx_generate_nacp(mi_app.nacp NAME "Nombre App" AUTHOR "Autor" VERSION "1.0.0")
nx_create_nro(mi_app NACP mi_app.nacp ICON icon.jpg)
```

Comando de build desde MSYS2 devkitPro:
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
make -j$(nproc)
```

---

## OpenGL ES 3.0 vs Desktop OpenGL 3.3

| Aspecto | Desktop (PC) | Switch (GLES) |
|---|---|---|
| Header | `<glad/glad.h>` o similar | `<GLES3/gl3.h>` |
| Context SDL | `SDL_GL_CONTEXT_PROFILE_CORE` 3.3 | `SDL_GL_CONTEXT_PROFILE_ES` 3.0 |
| Shader header | `#version 330 core` | `#version 300 es` + `precision mediump float;` |
| Function pointers | Necesarios (glad/glew) | No — funciones directas |

Patrón recomendado para código dual PC/Switch:
```c
#ifdef NINTENDO_SWITCH
#  include <GLES3/gl3.h>
#  define GLSL_VERSION "#version 300 es\nprecision mediump float;\n"
#else
#  include <glad/glad.h>
#  define GLSL_VERSION "#version 330 core\n"
#endif
```

---

## Ventana y contexto SDL2

```c
// Switch: siempre 1280x720 fullscreen
#ifdef NINTENDO_SWITCH
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_Window *window = SDL_CreateWindow("App",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
#endif
```

---

## Botones del controlador — TRAMPA IMPORTANTE

SDL2 usa un mapeo POSICIONAL estilo Xbox. En un Joy-Con de Switch:

| Botón físico Switch | SDL2 constante |
|---|---|
| A (derecha) | `SDL_CONTROLLER_BUTTON_B` |
| B (abajo) | `SDL_CONTROLLER_BUTTON_A` |
| X (arriba) | `SDL_CONTROLLER_BUTTON_Y` |
| Y (izquierda) | `SDL_CONTROLLER_BUTTON_X` |

**Consecuencia**: si querés que el botón físico A confirme y B cancele (convención Nintendo):
```c
case SDL_CONTROLLER_BUTTON_B: /* A físico */ accion_confirmar(); break;
case SDL_CONTROLLER_BUTTON_A: /* B físico */ accion_cancelar(); break;
```

---

## Keycodes SDL2 vs SDL1

SDL2 cambió los valores de las teclas de flecha. **No usar números hardcodeados**:

```c
// MAL (valores de SDL1, no funcionan en SDL2):
if (key == 273) // nunca es SDLK_UP

// BIEN:
if (key == SDLK_UP)    // 1073741906
if (key == SDLK_DOWN)  // 1073741905
if (key == SDLK_LEFT)  // 1073741904
if (key == SDLK_RIGHT) // 1073741903
```

Los que SÍ coinciden con ASCII (se pueden usar como número o constante):
- `SDLK_RETURN` = 13
- `SDLK_ESCAPE` = 27
- `SDLK_TAB` = 9
- Letras y números: sus valores ASCII normales

---

## Stick analógico → eventos digitales

```c
#define AXIS_DEAD 8000

void handle_axis(SDL_GameControllerAxis axis, Sint16 value) {
    int neg = value < -AXIS_DEAD;
    int pos = value >  AXIS_DEAD;
    switch (axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            /* izquierda / derecha */
            break;
        case SDL_CONTROLLER_AXIS_LEFTY:
            /* arriba / abajo */
            break;
        /* etc. */
    }
}
```

L3 (click del stick izquierdo) = `SDL_CONTROLLER_BUTTON_LEFTSTICK`. Útil como botón de menú.

---

## Rutas en la SD

```
sdmc:/switch/nombre_app/       ← carpeta principal de la app
sdmc:/switch/nombre_app/rom.vb ← archivos de datos
sdmc:/switch/nombre_app/*.cfg  ← configuración guardada
```

La carpeta debe existir en la SD antes de que la app intente leer/escribir en ella.

---

## Instalar paquetes SDL2 en devkitPro

Desde PowerShell (no desde MSYS2):
```powershell
C:\devkitPro\msys2\usr\bin\pacman.exe -S switch-sdl2
```

Paquetes comunes:
- `switch-dev` — base (libnx, cmake toolchain)
- `switch-sdl2` — SDL2
- `switch-mesa` — GLES (EGL, GLESv2, glapi, drm_nouveau)

---

## Errores comunes y soluciones

| Error | Causa | Solución |
|---|---|---|
| `CMAKE_SYSTEM_NAME` check falla antes de compilar | Check puesto ANTES de `project()` | Mover `project()` primero, check después |
| SDL2 headers no encontrados aunque cmake lo encontró | SDL2 cmake config no propaga includes | Agregar `$DEVKITPRO/portlibs/switch/include` a `target_include_directories` |
| App crashea al arrancar en Switch | Ruta de archivo no existe en SD | Verificar que la carpeta `sdmc:/switch/nombre_app/` existe |
| `dkp-pacman: command not found` | Shell equivocado | Usar `C:\devkitPro\msys2\usr\bin\pacman.exe` desde PowerShell |
| Botones A/B invertidos | SDL2 mapeo posicional vs etiquetas físicas Switch | Ver sección de botones arriba |
| Menú se controla con analógico derecho | Comparaciones con 273/274 (SDL1) en lugar de SDLK_UP/DOWN | Usar siempre las constantes SDLK_* |

---

## Emerald Launcher 2.x — notas del port

- **Idioma del sistema**: `setInitialize()` + `setGetSystemLanguage(&code)` devuelve el
  código como texto empaquetado en un `u64` ("es", "es-419", "en-US"...); basta con
  comparar los dos primeros caracteres (`src/i18n.cpp`).
- **Memoria en uso** (HUD de rendimiento): `svcGetInfo(&used, InfoType_UsedMemorySize,
  CUR_PROCESS_HANDLE, 0)` (`src/perf.cpp`).
- **Save states con cores estáticos**: `retro_serialize_size`, `retro_serialize`,
  `retro_unserialize`, `retro_reset` y `retro_set_controller_port_device` ya estaban en la
  lista de símbolos que `objcopy` prefija por core; `src/core.c` los declara con
  `CORE_EXTERNS` y los guarda en `BuiltinCore`.
- **Audio**: el juego abre su propio dispositivo a 48 kHz y remuestrea con control
  dinámico de tasa (±0,5 %) para que la cola no se vacíe ni se llene (`src/audio.cpp`).
  El dispositivo del menú (efectos + música) se cierra mientras corre un juego; los
  efectos que suenan en ese momento se mezclan en el audio del juego.
- **Botones**: SDL numera los botones del Switch por posición (estilo Xbox): el botón A
  (derecha) es `SDL_CONTROLLER_BUTTON_B`. Los valores por defecto y las etiquetas de la
  pantalla de Controles lo tienen en cuenta (`src/input.cpp`).
- **Archivos**: todo lo del usuario (prefs.json, stats.json, input.json, saves/states/,
  capturas) vive junto a db.json en `sdmc:/emerald/`, y se escribe con archivo temporal +
  rename (`fs_write_atomic`); en el Switch el rename no reemplaza un archivo existente, así
  que se borra el destino antes.
- **Cores estáticos**: `tools/build-switch-cores.sh` los clona en `cores-src/` y los
  compila con `make platform=libnx`, salvo mGBA: su repositorio de libretro ya no trae
  `Makefile.libretro` y el core se compila con CMake (`-DLIBMGBA_ONLY=ON
  -DBUILD_LIBRETRO=ON -DLIBRETRO_STATIC=ON -DLIBRETRO_SUFFIX=_libnx`, objetivo
  `mgba_libretro`). CI usa la imagen `devkitpro/devkita64`.
