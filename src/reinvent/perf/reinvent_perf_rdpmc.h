#pragma once

// Purpose: Read and store Intel's Architectural Performance Events from its PMU (Performance Monitor Unit)
//
// Classes:
//   Perf:::Rdpmc: 
// 

#include <assert.h>
#include <x86intrin.h>

namespace Reinvent {
namespace Perf {

class Rdpmc {
public:
  // ENUM
  enum Event {
    UNHALTED_CORE_CYCLES      = 0x003c,
    INSTRUCTIONS_RETIRED      = 0x00c0,
    LLC_REFERENCES            = 0x4f2e,
    LLC_MISSES                = 0x412e,
  };
 
private:
  // DATA
  int            d_max;
  int            d_index;
  unsigned long *d_value;

public:
  Rdpmc(int count, bool pin);
    // Create Rdpmc object to collect up to specified 'count' PMU measurements. The behavior is defined provided
    // 'count>0'. If 'pin' true, the caller's thread is pinned to the HW core it's running on. Note: pass 'pin=false'
    // if caller's thread is already pinned.
 
  ~Rdpmc();
    // Destroy this object
 
  // ACCESSORS
  int maxIndex() const;
    // Return 'count' provided at construction time.

  int lastIndex() const;
    // Return -1 if there are no collected values otherwise return '0<=n' where 'n' is the zero based index of the
    // last value collected.

  int value(int i, unsigned long *unhaltedCoreCycles, unsigned long *instRetired, unsigned long *llcReferences,
    unsigned long *llcMisses) const;
    // Return 0 setting specified 'instRetired, llcReferences, llcMisses' to the specified 'i'th snapshot values.
    // The behavior is defined provided 'lastIndex()!=-1' and '0<=i<=lastIndex()'.

  int cpu() const;
    // Return the zero-based HW core number the caller is running on

  // PRIVATE MANIPULATORS
  int rdmsr(unsigned int reg, int cpu, unsigned long *value);
    // Return zero if read from specified Intel PMU register 'reg' on specified 'cpu' current value writing it into
    // 'value' and non-zero otherwise;
  
  int wrmsr(unsigned int reg, int cpu, unsigned long data);
    // Return zero if wrote to specified Intel PMU register 'reg' on specified 'cpu' the specified value 'data' and
    // non-zero otherwise
 
  // MANIUPLATORS
  int pinCpu();
    // Return 0 if the caller's thread was pinned to the CPU returned by 'cpu()' and non-zero otherwise.

  int snapshot(void); 
    // Return 0 if the current value of all PMU counters is taken and cached and non-zero otherwise. Upon return
    // 'lastIndex' will increase by one. The behavior is defined provided, prior to call, 'lastIndex()'
    // is less than 'maxIndex()'-1.

  unsigned long readCounter(int cntr);
    // Read and return the current value of the specified '0<=cntr<=3' counter
//
};

// CREATORS
inline
Rdpmc::~Rdpmc() {
  if (d_value) {
    delete [] d_value;
  }
  d_value = 0;
};

// ACCESSORS
inline
int Rdpmc::maxIndex() const {
  return d_max;
}

inline
int Rdpmc::lastIndex() const {
  return (d_index==-1) ? -1 : ((d_index+1)>>2)-1;
}

inline
int Rdpmc::value(int i, unsigned long *unhaltedCoreCycles, unsigned long *instRetired, unsigned long *llcReferences,
    unsigned long *llcMisses) const {
  assert(i>=0);
  assert(i<d_max);
  assert(lastIndex()!=-1);
  assert(i<=lastIndex());
  assert(unhaltedCoreCycles);
  assert(instRetired);
  assert(llcReferences);
  assert(llcMisses);

  i<<=2;

  *unhaltedCoreCycles = d_value[i];
  *instRetired = d_value[++i];
  *llcReferences = d_value[++i];
  *llcMisses = d_value[++i];

  return 0;
}

// MANIPULATORS
inline
unsigned long Rdpmc::readCounter(int cntr) {
  assert(cntr>=0&&cntr<=3);
  __asm __volatile("lfence");
  return __rdpmc(cntr);
}

} // namespace Perf
} // namespace Reinvent 
