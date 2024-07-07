#include <stddef.h>
#include <stdbool.h>

extern volatile bool isGamePaused;
extern volatile bool isGameTicking;
extern volatile double tickTime;
extern volatile double tickDelay;
extern volatile bool multiTickPerFrame;
extern volatile size_t gameTPS;

void tsc_setupUpdateThread();
void tsc_signalUpdateShouldHappen();
