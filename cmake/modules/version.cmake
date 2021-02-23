execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND date "+%d %b %Y"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE BUILD_DATE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

configure_file (
  ${CMAKE_SOURCE_DIR}/lib/Version.cpp
  ${CMAKE_BINARY_DIR}/lib/Version.cpp
  @ONLY
)

add_library(MullXCTestVersion ${CMAKE_BINARY_DIR}/lib/Version.cpp)
