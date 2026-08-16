add_library(templar_common STATIC
  src/Protocol.cpp
)

target_include_directories(templar_common PUBLIC include)
