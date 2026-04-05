# SCV4

A game built on the [Tilengine](https://github.com/megamarc/Tilengine) retro-graphics engine.

## Prerequisites

* **Tilengine** — built or installed so that CMake can find the library and headers.
* **SDL3**
* **zlib** / **libpng**
* A C23-capable compiler

## Building

```bash
mkdir build && cd build
cmake -DTILENGINE_DIR=/path/to/Tilengine ..
cmake --build .
```

Set `TILENGINE_DIR` to the root of a Tilengine source checkout (the `FindTilengine.cmake` module will locate `src/Tilengine.h` and the built library automatically).

If Tilengine is installed system-wide the variable can be omitted.

## Running

```bash
cd build
./intro
```

## Tests

```bash
cd build
ctest
```

### Coverage

```bash
cmake -DCOVERAGE=ON ..
cmake --build . --target coverage
```
