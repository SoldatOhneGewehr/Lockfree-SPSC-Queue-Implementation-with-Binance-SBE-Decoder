cmake_minimum_required(VERSION 3.10)
project(BinanceSBEClient VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${OPENSSL_INCLUDE_DIR})

# Source files
set(SOURCES
    src/main.cpp
    src/BinanceSBEWebSocket.cpp
    src/LockFree.cpp
)

# Header files (optional, for IDE support)
set(HEADERS
    include/BinanceSBEWebSocket.hpp
    include/LockFree.hpp
)

# Create executable
add_executable(binance_client ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(binance_client
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    Threads::Threads
)

# Compiler warnings
if(MSVC)
    target_compile_options(binance_client PRIVATE /W4)
else()
    target_compile_options(binance_client PRIVATE -Wall -Wextra -pedantic)
endif()

# Print configuration info
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")