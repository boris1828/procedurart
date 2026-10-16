# Adding a piece

1. Create `pieces/<name>/` with a `CMakeLists.txt` containing `add_piece(<name> main.cpp)`.
2. In `main.cpp`, subclass `core::Piece` and call `core::run(piece)` from `main()`.
3. Re-run CMake; build with `cmake --build build --config Release --target <name>`.
