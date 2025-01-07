#ifndef TSC_GRAPHICS_RENDERING
#define TSC_GRAPHICS_RENDERING

#include "../cells/grid.h"
#include "ui.h"

typedef struct camera_t {
    double x, y, cellSize, speed;
} camera_t;

extern const char *currentId;
extern char currentRot;
extern camera_t renderingCamera;

typedef union tsc_categorybutton {
    ui_button *cell;
    ui_button *button;
    struct {
        ui_button *category;
        union tsc_categorybutton *items;
    };
} tsc_categorybutton;

void tsc_setupRendering();
void tsc_resetRendering();
int tsc_cellMouseX();
int tsc_cellMouseY();
void tsc_drawGrid();
void tsc_handleRenderInputs();
void tsc_pasteGridClipboard();
void tsc_drawCell(tsc_cell *cell, int x, int y, double opacity, int gridRepeat, bool forceRectangle);

#endif
