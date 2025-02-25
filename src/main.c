#define TSC_MULTIPLAYER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cells/grid.h"
#include "cells/subticks.h"
#include "saving/saving.h"
#include "threads/workers.h"
#include <raylib.h>
#include "graphics/resources.h"
#include "graphics/rendering.h"
#include "utils.h"
#include "api/api.h"
#include "graphics/ui.h"
#include "graphics/resources.h"
#include "cells/ticking.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#ifdef TSC_MULTIPLAYER
#include "multiplayer/multiplayer.h"
#include <math.h>
#include <assert.h>
#endif // TSC_MULTIPLAYER

// dont worry about this this would never fuck shit up
void *rp_resourceTableGet(tsc_resourcetable *table, const char *id); 

ui_frame *tsc_mainMenu;
ui_frame *tsc_creditsMenu;

typedef struct tsc_mainMenuBtn_t {
    ui_button *play;
#ifdef TSC_MULTIPLAYER
    ui_button *multiplayer;
#endif // TSC_MULTIPLAYER
    ui_button *settings;
    ui_button *credits;
    ui_button *texturepacks;
    ui_button *quit;
} tsc_mainMenuBtn_t;

tsc_mainMenuBtn_t tsc_mainMenuBtn;

#define TSC_MAINMENU_PARTICLE_COUNT 8192

typedef struct tsc_mainMenuParticle_t {
    const char *id;
    float r;
    float g;
    float dist;
    float angle;
    float rot;
} tsc_mainMenuParticle_t;

float tsc_randFloat() {
    return (float)rand() / RAND_MAX;
}

tsc_mainMenuParticle_t tsc_randomMainMenuParticle(bool respawn) {
    const char *builtinCells[] = {
        builtin.push, builtin.wall, builtin.enemy, builtin.mover,
        builtin.trash, builtin.slide, builtin.generator, builtin.rotator_cw,
        builtin.rotator_ccw,
    };
    size_t builtinCellCount = sizeof(builtinCells) / sizeof(const char *);

    int w = GetScreenWidth();
    int h = GetScreenHeight();
    int r = (w < h ? w : h) / 4;
    int m = w > h ? w : h;

    tsc_mainMenuParticle_t particle;
    particle.id = builtinCells[rand() % builtinCellCount];
    particle.angle = tsc_randFloat() * 2 * PI;
    particle.r = tsc_randFloat() * (float)r / 10;
    particle.g = tsc_randFloat() * (r/particle.r) * 4;
    particle.dist = r + tsc_randFloat() * r * 4;
    if(respawn) {
        particle.dist = m + tsc_randFloat() * r;
    }
    particle.rot += tsc_randFloat() * 2 * PI;
    return particle;
}

float tsc_magicMusicSampleDoNotTouchEver = 0;

void tsc_magicStreamProcessorDoNotUseEver(void *buffer, unsigned int sampleCount) {
    if(tsc_streql(tsc_currentMenu, "game")) return;
    float *samples = buffer;
    if(sampleCount > 0) {
        tsc_magicMusicSampleDoNotTouchEver = 0;
        for(size_t i = 0; i < sampleCount; i++) {
            tsc_magicMusicSampleDoNotTouchEver += fabs(samples[i]);
        }
        tsc_magicMusicSampleDoNotTouchEver /= sampleCount;
    }
}

#define TSC_GRID_SIZE_BUFSIZE 16

#ifdef TSC_MULTIPLAYER
#define TSC_MP_SERVER_BUFSIZE 64
void            *           tsc_serverReciever = NULL;
uint16_t tsc_clientId = 0;
#endif // TSC_MULTIPLAYER

int main(int argc, char **argv) {
    srand(time(NULL));

    // Suppress raylib debug messages
    SetTraceLogLevel(LOG_ERROR);

    workers_setupBest();

    tsc_init_builtin_ids();

    char *level = NULL;
    char gridWidth[TSC_GRID_SIZE_BUFSIZE];
    char gridHeight[TSC_GRID_SIZE_BUFSIZE];
    bool gridWidthSel, gridHeightSel = false;
    strcpy(gridWidth, "100");
    strcpy(gridHeight, "100");
    for(int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if(strncmp(arg, "--width=", 8) == 0) {
            char *svalue = arg + 8;
            strncpy(gridWidth, svalue, TSC_GRID_SIZE_BUFSIZE-1);
        } else if(strncmp(arg, "--height=", 9) == 0) {
            char *svalue = arg + 9;
            strncpy(gridHeight, svalue, TSC_GRID_SIZE_BUFSIZE-1);
        } else if(strncmp(arg, "--level=", 8) == 0) {
            level = arg + 8;
        } else if(strcmp(arg, "--mtpf") == 0) {
            multiTickPerFrame = true;
        } else if(strcmp(arg, "--no-mtpf") == 0) {
            multiTickPerFrame = false;
        } else if(strncmp(arg, "--tickDelay=", 12) == 0) {
            tickDelay = atof(arg + 12);
        }
    }
    
    tsc_subtick_addCore();
    tsc_saving_registerCore();
    tsc_loadDefaultCellBar();

    tsc_mainMenu = tsc_ui_newFrame();
    tsc_creditsMenu = tsc_ui_newFrame();


    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "The Sandbox Cell");
    SetWindowMonitor(0);
    SetWindowState(FLAG_MSAA_4X_HINT | FLAG_WINDOW_MAXIMIZED);

    double timeElapsed = 0;

    // L + ratio
    SetExitKey(KEY_NULL);

    InitAudioDevice();
   
    const char *defaultRP = tsc_strintern("default");
    tsc_createResourcePack(defaultRP);
    defaultResourcePack = tsc_getResourcePack(defaultRP);
    if(defaultResourcePack == NULL) {
        fprintf(stderr, "Default texture pack is missing.\n");
        return 1;
    }

    size_t allPackC = 0;
    char **allPacks = tsc_dirfiles("resources", &allPackC);
    for(size_t i = 0; i < allPackC; i++) {
        if(!tsc_streql(allPacks[i], "default")) {
            tsc_createResourcePack(tsc_strintern(allPacks[i]));
        }
    }
    tsc_freedirfiles(allPacks);

    tsc_loadSettings();

    if(tsc_toBoolean(tsc_getSetting(builtin.settings.fullscreen))) {
        tsc_settingHandler(builtin.settings.fullscreen);
    }

    tsc_loadAllMods();
    
    tsc_enableResourcePack(defaultResourcePack);
    tsc_setupRendering();

    tsc_mainMenuBtn.play = tsc_ui_newButtonState();
    tsc_mainMenuBtn.quit = tsc_ui_newButtonState();
    tsc_mainMenuBtn.texturepacks = tsc_ui_newButtonState();
    tsc_mainMenuBtn.settings = tsc_ui_newButtonState();
    tsc_mainMenuBtn.credits = tsc_ui_newButtonState();

#ifdef TSC_MULTIPLAYER
    tsc_mainMenuBtn.multiplayer = tsc_ui_newButtonState();
    float last_cursor_x = 0.0;
    float last_cursor_y = 0.0;
    Vector3 mpColor = {0.75, 0.0, 1.0};
    char mpName[17] = "";
    char mpServer[TSC_MP_SERVER_BUFSIZE] = "";
    bool mpNameSel, mpServerSel = false;
#endif // TSC_MULTIPLAYER

    int off = 0;
    
    tsc_mainMenuParticle_t mainMenuParticles[TSC_MAINMENU_PARTICLE_COUNT];
    size_t mainMenuParticleCount = 0;
    bool particlesInitialized = false;

    AttachAudioMixedProcessor(tsc_magicStreamProcessorDoNotUseEver);

    double particleHalvingTimer = 0;

    double blackHoleSoundBonus = 0;

    tsc_addCoreSplashes();
    const char *splash = tsc_randomSplash();

    const char *creditsContent = tsc_hasfile("CREDITS.txt") ? tsc_allocfile("CREDITS.txt", NULL) : NULL;

    while(!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(GetColor(tsc_queryOptionalColor("bgColor", 0x171c1fFF)));

        int width = GetScreenWidth();
        int height = GetScreenHeight();

        if(!particlesInitialized && timeElapsed > 0.2) {
            particlesInitialized = true;
            mainMenuParticleCount = TSC_MAINMENU_PARTICLE_COUNT;
            for(size_t i = 0; i < mainMenuParticleCount; i++) {
                const char *builtinCells[] = {
                    builtin.push, builtin.wall, builtin.enemy, builtin.mover,
                    builtin.trash, builtin.slide, builtin.generator, builtin.rotator_cw,
                    builtin.rotator_ccw,
                };
                size_t builtinCellCount = sizeof(builtinCells) / sizeof(const char *);
                mainMenuParticles[i] = tsc_randomMainMenuParticle(false);
                mainMenuParticles[i].id = builtinCells[(i / (mainMenuParticleCount / builtinCellCount)) % builtinCellCount];
            }
        }

        timeElapsed += GetFrameTime();
        blackHoleSoundBonus = tsc_magicMusicSampleDoNotTouchEver;

        particleHalvingTimer -= GetFrameTime();

        if(tsc_streql(tsc_currentMenu, "game")) {
            tsc_drawGrid();
#ifdef TSC_MULTIPLAYER
            tsc_clientId = tsc_mp_getMyId();
            if (tsc_mp_isMultiplayer() && tsc_clientId != 0) {
                assert(tsc_serverReciever != NULL);
                tsc_mp_processMessage(tsc_serverReciever);

                int clients = tsc_mp_getClientsLen();
                for (int i = 0; i < clients; i++) {
                    tsc_mp_client_t client = tsc_mp_getClient(i);
                    if (client.id == tsc_clientId) {
                        DrawText(
                            TextFormat("%d", tsc_clientId),
                            GetMouseX(),
                            GetMouseY() + 20,
                            20,
                            WHITE
                        );
                        continue;
                    }
                    Vector2 pos = {
                        .x = (client.x * renderingCamera.cellSize) - renderingCamera.x,
                        .y = (client.y * renderingCamera.cellSize) - renderingCamera.y
                    };
                    Vector2 text_size = MeasureTextEx(GetFontDefault(), client.name, 0.4 * renderingCamera.cellSize, 0.1 * renderingCamera.cellSize);
                    Vector2 text_pos = {
                        .x = pos.x - (text_size.x / 2.0) + (renderingCamera.cellSize / 2.0),
                        .y = pos.y + renderingCamera.cellSize
                    };
                    Color tint = {
                        .r = client.r,
                        .g = client.g,
                        .b = client.b,
                        .a = 255
                    };

                    DrawTextureEx(
                        textures_get(tsc_strintern("cursor")),
                        pos,
                        0.0f,
                        (renderingCamera.cellSize / 512.0f) * 1.5,
                        tint
                    );

                    // Hopefully held string doesnt keep being used somewhere :staring_cat:
                    char* held = tsc_mp_getHeldCell(i);
                        tsc_cell cell = tsc_cell_create(tsc_strintern(held), 0);
                            Texture tex = textures_get(cell.id);
                            DrawTextureEx(
                                tex,
                                (Vector2) {
                                    .x = pos.x + (renderingCamera.cellSize * 0.75f),
                                    .y = pos.y + (renderingCamera.cellSize * 0.45f)
                                },
                                0,
                                renderingCamera.cellSize / 64.0f,
                                WHITE
                            );
                            //tsc_drawCell(&cell, (int)client.x, (int)client.y, 0.5, 1, false);
                        tsc_cell_destroy(cell);
                    tsc_mp_freeHeldCell(held);

                    DrawTextEx(GetFontDefault(), client.name, text_pos, 0.4 * renderingCamera.cellSize, 0.1 * renderingCamera.cellSize, tint);
                }
            
                float cursor_cell_x = GetMouseX() +  renderingCamera.x;
                cursor_cell_x /= renderingCamera.cellSize;
                float cursor_cell_y = GetMouseY() + renderingCamera.y;
                cursor_cell_y /= renderingCamera.cellSize;

                // If cursor has changed more than 0.1 in any axis update the cursor
                if ((fabs(last_cursor_x - cursor_cell_x) > 0.1) || (fabs(last_cursor_y - cursor_cell_y) > 0.1)) {
                    tsc_mp_moveCursor(cursor_cell_x, cursor_cell_y);
                    last_cursor_x = cursor_cell_x;
                    last_cursor_y = cursor_cell_y;
                }
            }
#endif // TSC_MULTIPLAYER
        } else {
            // Dont worry potato PCs we got you... though credits will kill you
            if(GetFPS() < 30 && timeElapsed > 0.5 && particleHalvingTimer <= 0 && !tsc_streql(tsc_currentMenu, "credits")) {
                particleHalvingTimer = 0.5;
                mainMenuParticleCount /= 2;
                for(size_t i = 0; i < mainMenuParticleCount; i++) {
                    const char *builtinCells[] = {
                        builtin.push, builtin.wall, builtin.enemy, builtin.mover,
                        builtin.trash, builtin.slide, builtin.generator, builtin.rotator_cw,
                        builtin.rotator_ccw,
                    };
                    size_t builtinCellCount = sizeof(builtinCells) / sizeof(const char *);
                    double f = (double)i / ((double)mainMenuParticleCount / (double)builtinCellCount);
                    mainMenuParticles[i].id = builtinCells[(size_t)(f * builtinCellCount) % builtinCellCount];
                }
            }
            // Super epic background
            int r = (width < height ? width : height) / 4;
            int bx = width/2;
            int by = height/2;
            for(size_t i = 0; i < mainMenuParticleCount; i++) {
                tsc_mainMenuParticle_t particle = mainMenuParticles[i];
                Texture t = textures_get(particle.id);
                Vector2 pos = {
                    bx + cos(particle.angle) * particle.dist,
                    by + sin(particle.angle) * particle.dist,
                };
                float scale = particle.r/t.width;
                Color c = WHITE;
                float x = r/particle.dist;
                c.a = x > 1 ? 255 : x * 255;
                Vector2 origin = {particle.r/2, particle.r/2};
                DrawTexturePro(t,
                    (Rectangle) {0, 0, t.width, t.height},
                    (Rectangle) {pos.x, pos.y, particle.r, particle.r},
                   origin, particle.rot * 180 / PI, c
                );
            }
            float blackHoleExtra = blackHoleSoundBonus;
            float blackHoleLimit = 1;
            if(blackHoleExtra > blackHoleLimit) blackHoleExtra = blackHoleLimit;
            DrawCircle(bx, by, r * (1 + blackHoleExtra), GetColor(0x0b0c0dFF)); // BLACK HOLE
        }

        if(tsc_streql(tsc_currentMenu, "main")) {
            tsc_ui_pushFrame(tsc_mainMenu);
            int textHeight = 100;
            tsc_ui_text("The Sandbox Cell v0.1.0", 50, WHITE);
            tsc_ui_pad(20, 20);
            tsc_ui_align(0.5, 0, width, textHeight);
            tsc_ui_text(splash, 25, WHITE);
            tsc_ui_pad(10, 70);
            tsc_ui_align(0.5, 0, width, textHeight);
            Color buttonHoverColor = GetColor(0x3275A8FF);
            tsc_ui_row({
                tsc_ui_text("Play", 20, WHITE);
                tsc_ui_pad(10, 10);
                if(tsc_ui_button(tsc_mainMenuBtn.play) == UI_BUTTON_HOVER) {
                    tsc_ui_box(buttonHoverColor);
                }
#ifdef TSC_MULTIPLAYER
                tsc_ui_text("Multiplayer", 20, WHITE);
                tsc_ui_pad(10, 10);
                if(tsc_ui_button(tsc_mainMenuBtn.multiplayer) == UI_BUTTON_HOVER) {
                    tsc_ui_box(buttonHoverColor);
                }
#endif // TSC_MULTIPLAYER
                tsc_ui_text("Settings", 20, WHITE);
                tsc_ui_pad(10, 10);
                if(tsc_ui_button(tsc_mainMenuBtn.settings) == UI_BUTTON_HOVER) {
                    tsc_ui_box(buttonHoverColor);
                }
                tsc_ui_text("Texture Packs", 20, WHITE);
                tsc_ui_pad(10, 10);
                if(tsc_ui_button(tsc_mainMenuBtn.texturepacks) == UI_BUTTON_HOVER) {
                    tsc_ui_box(buttonHoverColor);
                }
                tsc_ui_text("Credits", 20, WHITE);
                tsc_ui_pad(10, 10);
                if(tsc_ui_button(tsc_mainMenuBtn.credits) == UI_BUTTON_HOVER) {
                    tsc_ui_box(buttonHoverColor);
                }
                tsc_ui_text("Quit", 20, WHITE);
                tsc_ui_pad(10, 10);
                if(tsc_ui_button(tsc_mainMenuBtn.quit) == UI_BUTTON_HOVER) {
                    tsc_ui_box(buttonHoverColor);
                }
            });
            tsc_ui_pad(20, textHeight+20);
            tsc_ui_align(0.5, 0, width, 0);
            tsc_ui_render();
            tsc_ui_popFrame();
        } else {
            splash = tsc_randomSplash();
        }
        if(tsc_streql(tsc_currentMenu, "settings")) {
            GuiEnable();
            GuiLoadStyleDefault();
            GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
            if(GuiButton((Rectangle) { 20, 20, 100, 50 }, "Back")) {
                tsc_currentMenu = "main";
                tsc_resetRendering();
            }
            size_t titleSize = 128;
            size_t settingSize = 64;
            size_t settingSpacing = 20;
            size_t settingVertOff = 70;
            size_t settingDeadSpace = 50;
            BeginScissorMode(0, settingVertOff, width, height - settingVertOff);
            size_t curY = settingVertOff;
            Font font = GetFontDefault();
            for(size_t i = 0; i < tsc_settingLen; i++) {
                tsc_settingCategory cat = tsc_settingCategories[i];
                GuiSetStyle(DEFAULT, TEXT_SIZE, titleSize);
                int titleWidth = MeasureText(cat.title, titleSize);
                GuiLabel((Rectangle) {settingDeadSpace, curY - off, titleWidth, titleSize}, cat.title);
                curY += titleSize + settingSpacing;
                for(size_t j = 0; j < cat.settinglen; j++) {
                    tsc_setting setting = cat.settings[j];
                    int settingWidth = MeasureText(setting.name, settingSize);
                    GuiSetStyle(DEFAULT, TEXT_SIZE, settingSize);
                    if(setting.kind == TSC_SETTING_TOGGLE) {
                        bool b = tsc_toBoolean(tsc_getSetting(setting.id));
                        bool old = b;
                        GuiCheckBox((Rectangle) {settingDeadSpace, curY - off, settingSize, settingSize}, setting.name, &b);
                        if(old != b) {
                            tsc_setSetting(setting.id, tsc_boolean(b));
                            if(setting.callback != NULL) {
                                setting.callback(setting.id);
                            }
                        }
                    } else if(setting.kind == TSC_SETTING_SLIDER) {
                        float v = tsc_toNumber(tsc_getSetting(setting.id));
                        float old = v;
                        int sliderWidth = width/4;
                        char *theValue = NULL;
                        asprintf(&theValue, "%.2f", v);
                        GuiSlider(
                            (Rectangle) {settingDeadSpace + settingWidth, curY - off, settingWidth + sliderWidth, settingSize},
                            setting.name, theValue, &v, setting.slider.min, setting.slider.max);
                        if(old != v) {
                            tsc_setSetting(setting.id, tsc_number(v));
                            if(setting.callback != NULL) {
                                setting.callback(setting.id);
                            }
                        }
                        free(theValue);
                    } else if(setting.kind == TSC_SETTING_INPUT) {
                        int textWidth = width/4;
                        GuiLabel((Rectangle) {settingDeadSpace, curY - off, settingWidth, settingSize}, setting.name);
                        Rectangle textbox = {
                            settingDeadSpace + settingWidth + 10, curY - off,
                            settingWidth + textWidth, settingSize,
                        };
                        if(GuiTextBox(textbox, setting.string.buffer, setting.string.bufferlen, setting.string.selected)) {
                            cat.settings[j].string.selected = !setting.string.selected;
                            tsc_value s = tsc_string(setting.string.buffer);
                            tsc_setSetting(setting.id, s);
                            tsc_destroy(s);
                            if(setting.callback != NULL) {
                                setting.callback(setting.id);
                            }
                        }
                        if(setting.string.charset != NULL) {
                            size_t l = 0;
                            for(size_t i = 0; setting.string.buffer[i] != '\0'; i++) {
                                bool contained = false;
                                for(size_t j = 0; setting.string.charset[j] != '\0'; j++) {
                                    if(setting.string.charset[j] == setting.string.buffer[i]) contained = true;
                                }
                                if(contained) {
                                   setting.string.buffer[l] = setting.string.buffer[i];
                                   l++;
                                }
                            }
                            setting.string.buffer[l] = '\0';
                        }
                    } else {
                        GuiLabel((Rectangle) {settingDeadSpace, curY - off, settingWidth, settingSize}, setting.name);
                    }
                    curY += settingSize + settingSpacing;
                }
            }
            EndScissorMode();
            size_t maxSize = curY;
            if(maxSize > height) {
                off = GuiScrollBar((Rectangle) {width-20, 0, 20, height}, off, 0, maxSize - height);
            }
        }

        if(tsc_streql(tsc_currentMenu, "texturepacks")) {
            GuiEnable();
            GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
            if(GuiButton((Rectangle) { 20, 20, 100, 50 }, "Back")) {
                tsc_currentMenu = "main";
                tsc_resetRendering();
            }
            float offset = 40;
            float texturePackTitleSize = 100;
            float texturePackAuthorSize = 30;
            const char *defaultTexturePackName = "Unnamed";
            const char *defaultTexturePackAuthor = "Unknown";

            float curY = 90;
            for(size_t i = 0; tsc_indexResourcePack(i) != NULL; i++) {
                tsc_resourcepack *pack = tsc_indexResourcePack(i);
                bool enabled = false;
                for(size_t j = 0; tsc_indexEnabledResourcePack(j) != NULL; j++) {
                    if(tsc_indexEnabledResourcePack(j) == pack) {
                        enabled = true;
                        break;
                    }
                }
                const char *name = pack->name == NULL ? defaultTexturePackName : pack->name;
                const char *authorSuffix = TextFormat("by %s", pack->author == NULL ? defaultTexturePackAuthor : pack->author);
                int nameWidth = MeasureText(name, texturePackTitleSize);
                GuiSetStyle(DEFAULT, TEXT_SIZE, texturePackTitleSize);
                if(GuiCheckBox((Rectangle) {offset + texturePackTitleSize, curY, texturePackTitleSize, texturePackTitleSize}, name, &enabled)) {
                    if(enabled) {
                        tsc_enableResourcePack(pack);
                    } else if(pack != defaultResourcePack) {
                        tsc_disableResourcePack(pack);
                    }
                }
                // this works for the wrong reason
                Texture *resource = rp_resourceTableGet(pack->textures, builtin.textures.icon);
                if(resource != NULL) {
                    DrawTexturePro(
                        *resource, (Rectangle) {0, 0, resource->width, resource->height},
                        (Rectangle) {offset, curY, texturePackTitleSize, texturePackTitleSize},
                        (Vector2) {0, 0}, 0, WHITE
                    );
                }
                GuiSetStyle(DEFAULT, TEXT_SIZE, texturePackAuthorSize);
                int authorWidth = MeasureText(authorSuffix, texturePackAuthorSize);
                GuiLabel(
                    (Rectangle) {
                        offset + nameWidth + texturePackTitleSize * 2,
                        curY + texturePackTitleSize - texturePackAuthorSize,
                        authorWidth, texturePackAuthorSize
                    },
                    authorSuffix);
                curY += texturePackTitleSize;
            }
        }
        if(tsc_streql(tsc_currentMenu, "credits")) {
            tsc_ui_bringBackFrame(tsc_creditsMenu);
            tsc_ui_text(creditsContent == NULL ? "No CREDITS.txt" : creditsContent, 50, WHITE);
            tsc_ui_align(0.5, 0.5, width, height);
            tsc_ui_render();
            tsc_ui_popFrame();
            GuiEnable();
            GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
            if(GuiButton((Rectangle) { 20, 20, 100, 50 }, "Back")) {
                tsc_currentMenu = "main";
                particleHalvingTimer = 2;
                tsc_resetRendering();
            }
        }
        if(tsc_streql(tsc_currentMenu, "play")) {
            GuiEnable();
            GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
            if(GuiButton((Rectangle) { 20, 20, 100, 50 }, "Back")) {
                tsc_currentMenu = "main";
                particleHalvingTimer = 2;
                tsc_resetRendering();
            }
            float inputWidth = (float)width/5;
            float inputSpacing = inputWidth/5;
            float inputHeight = inputWidth/3; // 3:1 aspect ratio
            float inputY = (float)height/2-inputHeight;

            GuiSetStyle(DEFAULT,TEXT_SIZE, inputHeight);
            if(GuiTextBox((Rectangle) {(float)width/2-inputWidth-inputSpacing, inputY, inputWidth, inputHeight}, gridWidth, TSC_GRID_SIZE_BUFSIZE, gridWidthSel)) {
                gridWidthSel = !gridWidthSel;
            }
            if(GuiTextBox((Rectangle) {(float)width/2+inputSpacing, inputY, inputWidth, inputHeight}, gridHeight, TSC_GRID_SIZE_BUFSIZE, gridHeightSel)) {
                gridHeightSel = !gridHeightSel;
            }
            GuiSetStyle(DEFAULT,TEXT_SIZE, inputSpacing);
            int xWidth = MeasureText("by", inputSpacing);
            GuiLabel((Rectangle) {(float)(width-xWidth)/2, inputY+(inputHeight-inputSpacing), xWidth, inputSpacing}, "by");


            GuiSetStyle(DEFAULT,TEXT_SIZE, inputHeight);
            int playWidth = MeasureText("Play", inputHeight);
            if(GuiLabelButton((Rectangle) {(float)(width-playWidth)/2, inputY+inputHeight+inputSpacing, playWidth, inputHeight}, "Play")) {
                if(strlen(gridWidth) == 0) {
                    goto invalid;
                }
                if(strlen(gridHeight) == 0) {
                    goto invalid;
                }
                if(strspn(gridWidth, "0123456789") != strlen(gridWidth)) {
                    goto invalid;
                }
                if(strspn(gridHeight, "0123456789") != strlen(gridHeight)) {
                    goto invalid;
                }
                int w = atoi(gridWidth);
                int h = atoi(gridHeight);
                if(w == 0) goto invalid;
                if(h == 0) goto invalid;
                tsc_grid *grid = tsc_createGrid("main", w, h, NULL, NULL);
                tsc_switchGrid(grid);
                if(level != NULL) {
                    tsc_saving_decodeWithAny(level, grid);
                }
                tsc_grid *initial = tsc_createGrid("initial", grid->width, grid->height, NULL, NULL);
                tsc_copyGrid(initial, grid);
                tsc_currentMenu = "game";
                tsc_resetRendering();
                goto valid;

                invalid:
                fprintf(stderr, "Invalid dimensions: %s x %s\n", gridWidth, gridHeight);
                valid:;
            }
        }

        EndDrawing();

        if(tsc_streql(tsc_currentMenu, "game")) {
            tsc_handleRenderInputs();
        } else {
            double delta = GetFrameTime();
            if(!IsWindowFocused()) {
                delta /= 2;
            }
            int r = (width < height ? width : height) / 4;
            int bx = width/2;
            int by = height/2;
            for(size_t i = 0; i < mainMenuParticleCount; i++) {
                tsc_mainMenuParticle_t particle = mainMenuParticles[i];
                Texture t = textures_get(particle.id);
                Vector2 pos = {
                    bx + cos(particle.angle) * particle.dist,
                    by + sin(particle.angle) * particle.dist,
                };
                float x = 0.5*r/(particle.dist-r);
                float y = x * sqrt(x);
                float z = x * pow(x, 0.1);
                particle.dist -= particle.g * delta;
                particle.angle += y * delta;
                particle.rot += y * 2 * PI * delta;
                if(particle.dist < (float)r-particle.r) {
                    const char *oldID = particle.id;
                    particle = tsc_randomMainMenuParticle(true);
                    particle.id = oldID; // phoenix told me something something GPU hates texture swapping
                }
                mainMenuParticles[i] = particle;
            }
        }
        double delta = GetFrameTime();
        if(tsc_streql(tsc_currentMenu, "main")) {
            tsc_ui_bringBackFrame(tsc_mainMenu);
            tsc_ui_update(delta);
            if(tsc_ui_checkbutton(tsc_mainMenuBtn.play) == UI_BUTTON_PRESS) {
                tsc_currentMenu = "play";
                tsc_resetRendering();
            }
#ifdef TSC_MULTIPLAYER
            if(tsc_ui_checkbutton(tsc_mainMenuBtn.multiplayer) == UI_BUTTON_PRESS) {
                tsc_currentMenu = "multiplayer";
                tsc_resetRendering();
                // // New tsc doesnt create a grid by default which causes a segfault so we create it here
                // tsc_grid *grid = tsc_createGrid("main", 1, 1, NULL, NULL);
                // tsc_switchGrid(grid);

                // tsc_serverReciever = tsc_mp_connectToServer("localhost:6969", "Calion", (tsc_mp_color_t) {
                //     .r = 255,
                //     .g = 255,
                //     .b = 255
                // });
            }
#endif // TSC_MULTIPLAYER
            if(tsc_ui_checkbutton(tsc_mainMenuBtn.settings) == UI_BUTTON_PRESS) {
                tsc_currentMenu = "settings";
                tsc_resetRendering();
            }
            if(tsc_ui_checkbutton(tsc_mainMenuBtn.texturepacks) == UI_BUTTON_PRESS) {
                tsc_currentMenu = "texturepacks";
                tsc_resetRendering();
            }
            if(tsc_ui_checkbutton(tsc_mainMenuBtn.credits) == UI_BUTTON_PRESS) {
                tsc_currentMenu = "credits";
                tsc_resetRendering();
            }
            if(tsc_ui_checkbutton(tsc_mainMenuBtn.quit) == UI_BUTTON_PRESS) {
                if(rand() % 50) {
                    break;
                }
                tsc_ui_checkbutton(NULL); // segfault hehe
            }
            tsc_ui_popFrame();
        }
#ifdef TSC_MULTIPLAYER
        if(tsc_streql(tsc_currentMenu, "multiplayer")) {
            GuiEnable();
            GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
            if(GuiButton((Rectangle) { 20, 20, 100, 50 }, "Back")) {
                tsc_currentMenu = "main";
                particleHalvingTimer = 2;
                tsc_resetRendering();
            }

            GuiSetStyle(DEFAULT,TEXT_SIZE, 50);
            GuiLabel((Rectangle) {20, 120, 300, 50}, "Server:");
            GuiSetStyle(DEFAULT,TEXT_SIZE, 20);
            if(GuiTextBox((Rectangle) {220, 120, 300, 50}, mpServer, TSC_MP_SERVER_BUFSIZE, mpServerSel)) {
                mpServerSel = !mpServerSel;
            }

            GuiSetStyle(DEFAULT,TEXT_SIZE, 50);
            GuiLabel((Rectangle) {20, 190, 300, 50}, "Name:");
            GuiSetStyle(DEFAULT,TEXT_SIZE, 20);
            if(GuiTextBox((Rectangle) {220, 190, 300, 50}, mpName, 16, mpNameSel)) {
                mpNameSel = !mpNameSel;
            }

            GuiColorPickerHSV((Rectangle){ 20, 260, 100, 100}, "Cursor Color", &mpColor);

            if(GuiButton((Rectangle) {20, 380, 100, 50}, "Play")) {
                if (strlen(mpServer) > 0 && strlen(mpName) > 0) {
                    tsc_currentMenu = "game";
                    tsc_resetRendering();

                    tsc_grid *grid = tsc_createGrid("main", 1, 1, NULL, NULL);
                    tsc_switchGrid(grid);

                    Color col = ColorFromHSV(mpColor.x, mpColor.y, mpColor.z);
                    printf("%d %d %d\n", col.r, col.g, col.b);
                    tsc_serverReciever = tsc_mp_connectToServer(mpServer, mpName, (tsc_mp_color_t) {
                        .r = col.r,
                        .g = col.g,
                        .b = col.b
                    });
                }
            }
        }
#endif // TSC_MULTIPLAYER

        if(IsWindowFocused()) {
            SetMasterVolume(1.0);
        } else {
            SetMasterVolume(0.2);
        }
        tsc_sound_playQueue();
        // This handles all music stuff.
        tsc_music_playOrKeep();
    }

    CloseWindow();
    CloseAudioDevice();

    return EXIT_SUCCESS;
}
