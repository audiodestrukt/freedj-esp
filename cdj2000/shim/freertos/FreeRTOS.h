#pragma once
// Cooperative tasks (main/coop.cpp): nothing interrupts a task, so a
// critical section needs no lock.
typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
