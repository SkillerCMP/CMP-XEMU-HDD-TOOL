# Development-only Win32 declaration facades

These headers allow GCC and Clang to syntax-check both complete GUI translation
units when a native Windows SDK or MinGW is unavailable. They declare API shapes
and deliberately provide no implementation. They are never in production CMake
include paths and are not distributed as runtime dependencies.

Passing this check is NOT Windows compilation, linkage, GUI rendering, input,
DPI, drag/drop, accessibility, or actual Win32 behavior validation. Build with
MinGW/Windows and run the native UI for those acceptance tests.
