#include <stdint.h>

typedef struct tsc_mp_client_t {
    uint16_t id;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    float x;
    float y;
    char name[16 + 1];
} tsc_mp_client_t;

typedef struct tsc_mp_color_t {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} tsc_mp_color_t;

extern int tsc_mp_getClientsLen();
extern tsc_mp_client_t tsc_mp_getClient(int i);
extern void* tsc_mp_connectToServer(const char* server, const char* name, tsc_mp_color_t color);
extern bool tsc_mp_isMultiplayer();
extern void tsc_mp_moveCursor(float x, float y);
extern void tsc_mp_processMessage(void* receiver);
extern uint16_t tsc_mp_getMyId();
extern char* tsc_mp_getHeldCell(int i);
extern void tsc_mp_freeHeldCell(char* cell);
extern void tsc_mp_placeCell(const char* id, char rot, int x, int y);