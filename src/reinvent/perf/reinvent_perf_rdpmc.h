#pragma once

// Purpose: Read and store Intel's Architectural Performance Events from its PMU (Performance Monitor Unit)
//
// Classes:
//   Perf:::Rdpmc: 
// 

#include <assert.h>

namespace Reinvent {
namespace Perf {

class Rdpmc {
public:
  // ENUM
  enum Event {
    UNHALTED_CORE_CYCLES      = 0,
    INSTRUCTIONS_RETIRED      = 1,
    UNHALTED_REFERENCE_CYCLES = 2,
    LLC_REFERENCES            = 3,
    LLC_MISSES                = 4,
    BRANCH_INSTRUCT_RETIRED   = 5,
    BRANCH_MISSES_RETIRED     = 6
  };
 
private:
  // DATA
  unsigned char *d_op;
  int            d_max;
  int            d_index;
  unsigned long *d_value;

public:
  Rdpmc(int count);
    // Create Rdpmc object to collect up to specified 'count' PMU measurements. The behavior is defined provided
    // 'count>0'.
 
  ~Rdpmc();
    // Destroy this object
 
  // ACCESSORS
  int maxIndex() const;
    // Return 'count' provided at construction time.

  int lastValueIndex() const;
    // Return -1 if there are no collected values otherwise return '0<=n' where 'n' is the zero based index of the
    // last value collected.

  int value(int index, unsigned long *value, Event *event);
    // Return 0 setting specified 'value' to the measurement at specified 'index' and specified 'event' describing the
    // kind of event 'value' is and non-zero otherwise. The behavior is defined provided 'lastValueIndex()!=-1' and
    // '0<=index<=lastValueIndex()'.

  void startLLCMisses();
    // Start counting LLC (last level cache) misses. Once run, caller should run code under test then immediately call
    // 'endLLCMisses' to capture the number of LLC misses since start. The behavior is defined provided
    // 'lastValueIndex()<maxIndex()' and any previous call to this method was logically stopped with 'endLLCMisses'.
    // Note that the measurement is relative to the caller's CPU core.

  void endLLCMisses();
    // Stop counting LLC (last level cache) misses and record number of LLC misses since the 'startLLCMisses' was run.
    // Upon return the measurement may be retrived by passing 'lastValueIndex()' for the formal argument 'index' to the
    // accessor 'value'. Note that the measurement is relative to the caller's CPU core.
//
};

// CREATORS
inline
Rdpmc::Rdpmc(int count)
: d_max(count)
, d_index(-1)
{
  assert(count>0);
  d_op = new unsigned char[count];
  d_value = new unsigned long[count];
  assert(d_op);
  assert(d_value);
}

inline
Rdpmc::~Rdpmc() {
  if (d_op) {
    delete [] d_op;
  }
  if (d_value) {
    delete [] d_value;
  }
  d_op = 0;
  d_value = 0;
};

// ACCESSORS
inline
int Rdpmc::maxIndex() const {
  return d_index;
}

inline
int Rdpmc::value(int index, unsigned long *value, Event *event) {
  if (index>=0 && index<=d_index) {
    *value = d_value[index];
    *event = static_cast<Rdpmc::Event>(d_op[index]);
    return 0;
  }
  return -1;
}

inline
void Rdpmc::startLLCMisses() {
  unsigned long a, d, c;
  c = (1<<30)+Event::LLC_MISSES;
  __asm__ volatile("rdpmc" : "=a" (a), "=d" (d) : "c" (c));
  c = ((unsigned long)a) | (((unsigned long)d) << 32);
  d_op[++d_index] = Event::LLC_MISSES;
  d_value[d_index] = c;
}

inline
void Rdpmc::endLLCMisses() {
  unsigned long a, d, c;
  c = (1<<30)+Event::LLC_MISSES;
  __asm__ volatile("rdpmc" : "=a" (a), "=d" (d) : "c" (c));
  c = ((unsigned long)a) | (((unsigned long)d) << 32);
  d_value[d_index] = c - d_value[d_index];
}

} // namespace Perf
} // namespace Reinvent 
