// Test support for tests/test_synth.py (m68k part): stands in for the one rt/irq.cpp function rt/synth.cpp calls.
#include "rt/irq.hpp"

namespace rt {
void irqSetInt4(void *) {}
}  // namespace rt
