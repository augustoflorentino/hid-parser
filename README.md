# hid-parser

DESCRICAO

## Build and install

```sh
cmake -S . -B build -DBUILD_SHARED_LIBS=ON
cmake --build build
cmake --install build --prefix /usr/local
```

Use it from CMake:

```cmake
find_package(hid-parser REQUIRED)
target_link_libraries(app PRIVATE hid-parser::hid-parser)
```

Or with pkg-config:

```sh
cc app.c $(pkg-config --cflags --libs hid-parser)
```

## Single-file form

`scripts/amalgamate.sh` writes `dist/hid-parser.h`. Copy it into your project and, in exactly
one `.c` file:

```c
#define HIDP_IMPLEMENTATION
#include "hid-parser.h"
```

Define `HIDP_STATIC` as well to keep every function private to that file.

## Usage

See [`examples/basic.c`](examples/basic.c). The header `include/hid-parser/hid-parser.h` is the
API reference.

Conventions:

- every function that can fail returns `HIDP_OK` or an error code; `hidp_strerror` describes it
- what `hidp_x_create` returns is released only by `hidp_x_destroy`, which accepts `NULL`
- functions never take ownership of their arguments; returned pointers belong to the library
- pass your own allocator to `hidp_buf_create`, or `NULL` for `malloc`/`free`

## Versioning

Semantic Versioning. Compatible releases bump the minor version; incompatible ones bump the
major version and the soname. See [CHANGELOG.md](CHANGELOG.md).

## License

MIT, see [LICENSE](LICENSE).
