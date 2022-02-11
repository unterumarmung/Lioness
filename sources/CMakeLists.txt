file(GLOB LIONESS_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")
file(GLOB LIONESS_HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.hpp")

add_subdirectory(Flatten)

file(GLOB LIONESS_FLATTEN_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "Flatten/*.cpp")
file(GLOB LIONESS_FLATTEN_HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "Flatten/*.hpp")

add_library(Lioness MODULE ${LIONESS_SOURCES} ${LIONESS_HEADERS} ${LIONESS_FLATTEN_SOURCES} ${LIONESS_FLATTEN_HEADERS})

include(FetchContent)

FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.10.5/json.tar.xz
        )

FetchContent_GetProperties(json)
if(NOT json_POPULATED)
    FetchContent_Populate(json)
    add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

target_link_libraries(Lioness PRIVATE nlohmann_json::nlohmann_json)