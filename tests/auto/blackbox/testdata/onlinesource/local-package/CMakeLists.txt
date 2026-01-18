cmake_minimum_required(VERSION 3.14)
project(testmodule VERSION 1.0.0)

# Header-only library for testing
add_library(testmodule INTERFACE)
target_include_directories(testmodule INTERFACE include)
