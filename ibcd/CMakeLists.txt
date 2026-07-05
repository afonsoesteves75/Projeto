cmake_minimum_required(VERSION 3.16)
project(IBCD LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(glad STATIC
    ${CMAKE_SOURCE_DIR}/external/glad2/src/gl.c
)
target_include_directories(glad PUBLIC
    ${CMAKE_SOURCE_DIR}/external/glad2/include
)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

add_executable(ibcd
    src/main.cpp
    src/OBJLoader.cpp
    src/Oversampler.cpp
    src/Slicer.cpp
    src/CollisionSystem.cpp
    src/Player.cpp
    src/SliceMetadata.cpp
)

target_include_directories(ibcd PRIVATE
    src
    external
)

target_link_libraries(ibcd PRIVATE
    glad
    glfw
    OpenGL::GL
    glm::glm
)

if(WIN32)
    target_compile_definitions(ibcd PRIVATE NOMINMAX)
endif()

if(NOT WIN32)
    target_link_libraries(ibcd PRIVATE ${CMAKE_DL_LIBS})
endif()

set_target_properties(ibcd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_command(TARGET ibcd POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:ibcd>/output/slices
)
