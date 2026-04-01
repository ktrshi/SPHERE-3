cmake_minimum_required(VERSION 3.16...3.21)
project(SPHERE-3)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
include(CTest)

# Optimized compiler flags for maximum performance
# -O3: Aggressive optimizations including loop vectorization
# -march=native: Optimize for the local CPU architecture
# -DNDEBUG: Disable assertions for release builds
# -flto: Link-Time Optimization for cross-module optimizations
# -funroll-loops: Unroll loops for better performance
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -flto -funroll-loops")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto")

#set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_BUILD_TYPE Release)

#----------------------------------------------------------------------------
# Find Geant4 package, activating all available UI and Vis drivers by default
# You can set WITH_GEANT4_UIVIS to OFF via the command line or ccmake/cmake-gui
# to build a batch mode only executable
#
option(WITH_GEANT4_UIVIS "Build example with Geant4 UI and Vis drivers" ON)
if (WITH_GEANT4_UIVIS)
    find_package(Geant4 REQUIRED ui_all vis_all)
else ()
    find_package(Geant4 REQUIRED)
endif ()

#----------------------------------------------------------------------------
# Setup Geant4 include directories and compile definitions
# Setup include directory for this project
#
include(${Geant4_USE_FILE})
include_directories(${PROJECT_SOURCE_DIR}/include)

# --- zstd ---
find_library(ZSTD_LIB zstd REQUIRED)
find_path(ZSTD_INCLUDE zstd.h REQUIRED)
message(STATUS "zstd library: ${ZSTD_LIB}")
message(STATUS "zstd include: ${ZSTD_INCLUDE}")

#----------------------------------------------------------------------------
# Locate sources and headers for this project
# NB: headers are included so they will show up in IDEs
#
file(GLOB sources ${PROJECT_SOURCE_DIR}/src/*.cc)
file(GLOB headers ${PROJECT_SOURCE_DIR}/include/*.hh)

#----------------------------------------------------------------------------
# Add the executable, and link it to the Geant4 libraries
#
add_executable(SPHERE-3 SPHERE-3.cpp ${sources} ${headers})
set_property(TARGET SPHERE-3 PROPERTY CXX_STANDARD 20)
set_property(TARGET SPHERE-3 PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET SPHERE-3 PROPERTY CXX_EXTENSIONS OFF)
target_include_directories(SPHERE-3 PRIVATE ${ZSTD_INCLUDE})
target_link_libraries(SPHERE-3 ${Geant4_LIBRARIES} ${ZSTD_LIB})

add_executable(test_background_operator
    test_background_operator.cpp
    src/MoshitWriter.cc
    src/MoshitReader.cc
    src/BackgroundOperator.cc)
target_include_directories(test_background_operator PRIVATE ${ZSTD_INCLUDE})
target_link_libraries(test_background_operator ${Geant4_LIBRARIES} ${ZSTD_LIB})
if(BUILD_TESTING)
    add_test(NAME test_background_operator COMMAND test_background_operator)
endif()

add_executable(build_background_operator
    build_background_operator.cpp
    src/MoshitReader.cc
    src/BackgroundOperator.cc)
target_include_directories(build_background_operator PRIVATE ${ZSTD_INCLUDE})
target_link_libraries(build_background_operator ${Geant4_LIBRARIES} ${ZSTD_LIB})

add_executable(compare_moshits
    compare_moshits.cpp
    src/MoshitReader.cc)
target_include_directories(compare_moshits PRIVATE ${ZSTD_INCLUDE})
target_link_libraries(compare_moshits ${Geant4_LIBRARIES} ${ZSTD_LIB})

set(SPHERE-3_SCRIPTS
        init_vis.mac
        vis.mac
)

foreach (_script ${SPHERE-3_SCRIPTS})
    configure_file(
            ${PROJECT_SOURCE_DIR}/${_script}
            ${PROJECT_BINARY_DIR}/${_script}
            COPYONLY
    )
endforeach ()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

#----------------------------------------------------------------------------
# Install the executable to 'bin' directory under CMAKE_INSTALL_PREFIX
#
install(TARGETS SPHERE-3 DESTINATION bin)
