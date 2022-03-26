#include <gtest/gtest.h>

#include <perf/reinvent_perf_rdpmc.h>

#include <errno.h>
#include <string.h>

using namespace Reinvent;

TEST(util_errno, basic) {  
  unsigned a;
  Perf::Rdpmc perf(10, true);
  EXPECT_TRUE(perf.maxIndex()==10);
  EXPECT_TRUE(perf.lastIndex()==-1);
  perf.snapshot();
  for (unsigned i=0; i<10000; ++i) {
    a+=1;
  }
  perf.snapshot();
  EXPECT_TRUE(perf.lastIndex()==1);
  unsigned long c0,c1,c2,c3;
  perf.value(0, &c0, &c1, &c2, &c3);
  printf("index 0 -> c0 %lu c1 %lu c2 %lu c3 %lu\n", c0, c1, c2, c3); 
  perf.value(1, &c0, &c1, &c2, &c3);
  printf("index 1 -> c0 %lu c1 %lu c2 %lu c3 %lu\n", c0, c1, c2, c3); 
}

int main(int argc, char **argv) {                                                                                       
  testing::InitGoogleTest(&argc, argv);                                                                                 
  return RUN_ALL_TESTS();                                                                                               
}
