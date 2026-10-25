#pragma once
#include <cstdint>
extern uint32_t test_millis;
inline unsigned long millis() { return test_millis; }
