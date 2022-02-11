set(LIONESS_FLATTEN_SOURCES ClangAttribute.cpp Flatten.cpp)
set(LIONESS_FLATTEN_HEADERS Flatten.hpp)

add_library(LionessFlatten MODULE Flatten.cpp Flatten.hpp)

target_compile_definitions(LionessFlatten PRIVATE LIONESS_STANDALONE)