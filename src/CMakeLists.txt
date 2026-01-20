# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
FILE(GLOB_RECURSE driver_sources ${CMAKE_SOURCE_DIR}/lib/drivers/*.*)

idf_component_register(SRCS ${app_sources} ${driver_sources}
                       INCLUDE_DIRS "." 
                                    "../lib/include"
                                    "../lib/drivers/dfplayer"
                                    "../lib/drivers/ws2812")
