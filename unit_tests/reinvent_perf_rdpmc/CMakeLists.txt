cmake_minimum_required(VERSION 3.16)

#
# To avoid conflicts with how the reinvent library is built which
# may or may not have logging turned on, these tests bring in all
# library code required. Build is not dependent on libreinvent.a
#

set(TEST_TARGET reinvent_perf_rdpmc_unit_test.tsk)

set(SOURCES                                                                                                             
  ../../src/reinvent/perf/reinvent_perf_rdpmc.cpp

  reinvent_perf_rdpmc_unit_test.cpp
) 

add_executable(${TEST_TARGET} ${SOURCES})

target_compile_options(${TEST_TARGET} PUBLIC -g)
target_include_directories(${TEST_TARGET} PUBLIC ../../src/reinvent)
target_link_libraries(${TEST_TARGET} PUBLIC gtest)

add_test(
    NAME    ${TEST_TARGET}
    COMMAND "<TARGET_FILE:${TEST_TARGET}>"
)
