add_library(qbsvariant INTERFACE
    variant.h variant.hpp
    )
target_include_directories(qbsvariant
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>)
