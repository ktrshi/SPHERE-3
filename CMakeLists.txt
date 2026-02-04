cmake_minimum_required(VERSION 3.16...3.21)
project(SPHERE-3)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

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
target_link_libraries(SPHERE-3 ${Geant4_LIBRARIES})

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