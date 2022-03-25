#include <gtest/gtest.h>

#include <perf/reinvent_perf_rdpmc.h>

#include <errno.h>
#include <string.h>

using namespace Reinvent;

TEST(util_errno, basic) {  
  unsigned a;
  Perf::Rdpmc perf(10);
  perf.startLLCMisses();
  for (unsigned i=0; i<1000000000; ++i) {
    a+=1;
  }
  perf.startLLCMisses();
  unsigned long val;
  Perf::Rdpmc::Event event;
  perf.value(0, &val, &event);
  printf("LLC misses: %lu\n", val);
}

int main(int argc, char **argv) {                                                                                       
  testing::InitGoogleTest(&argc, argv);                                                                                 
  return RUN_ALL_TESTS();                                                                                               
}
