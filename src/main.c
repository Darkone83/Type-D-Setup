/////////////////////////////
//      Type D Setup       //
//      By Darkone83       //
//     Built with NXDK     //
/////////////////////////////     

#include <hal/video.h>
#include <hal/debug.h>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "detect.h"
#include "send_cmd.h"
#include <nxdk/net.h>

#define SCREEN_WIDTH_DEF  640
#define SCREEN_HEIGHT_DEF 480
#define MUSIC_VOLUME      0.35f
#define DEVICE_MAX        4           // Numbered units (1-4)
#define DEVICE_BAR_SLOTS  5           // 4 normal + XL

static int screen_width = SCREEN_WIDTH_DEF, screen_height = SCREEN_HEIGHT_DEF;
static FILE* audio_file = NULL;

#define SCALEY(y) ((int)((float)(y) * screen_height / (float)SCREEN_HEIGHT_DEF))
#define SCALEX(x) ((int)((float)(x) * screen_width / (float)SCREEN_WIDTH_DEF))

static int highlight_idx = 0; // device highlight index
static int selected_idx  = -1; // selected device
static int focus_row = 0;      // 0 = menu, 1 = device bar
static int menu_selected = 0;
static bool aboutVisible = false;

const char* menu_items[] = {
    "Next Image",
    "Previous Image",
    "Random Image",
    "Display Mode",
    "",               // Removed Brightness label but keep space
    "WiFi Restart",
    "WiFi Forget",
    "Display On",
    "Display Off"
};

const char* menu_cmds[] = {
    "0001", "0002", "0003", "0004", NULL,
    "0030", "0031", "0060", "0061"
};

#define MENU_ITEM_COUNT (sizeof(menu_items)/sizeof(menu_items[0]))
#define MENU_COLS 3
#define MENU_ROWS ((MENU_ITEM_COUNT + MENU_COLS - 1) / MENU_COLS)

static void draw_octagon(SDL_Renderer* r, SDL_Rect rc, int margin, SDL_Color c) {
    int l = rc.x - margin, t = rc.y - margin;
    int w = rc.w + 2 * margin, h = rc.h + 2 * margin;
    SDL_Point pts[9] = {
        {l + margin,     t},
        {l + w - margin, t},
        {l + w,     t + margin},
        {l + w,     t + h - margin},
        {l + w - margin, t + h},
        {l + margin,     t + h},
        {l,         t + h - margin},
        {l,         t + margin},
        {l + margin,     t}
    };
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLines(r, pts, 9);
}

static void fill_octagon(SDL_Renderer* r, SDL_Rect rc, int margin, SDL_Color c) {
    int l = rc.x - margin, t = rc.y - margin;
    int w = rc.w + 2 * margin, h = rc.h + 2 * margin;
    SDL_Point pts[8] = {
        {l + margin,     t},
        {l + w - margin, t},
        {l + w,     t + margin},
        {l + w,     t + h - margin},
        {l + w - margin, t + h},
        {l + margin,     t + h},
        {l,         t + h - margin},
        {l,         t + margin}
    };
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int y = t; y <= t + h; y++) {
        int x[8], n = 0;
        for (int i = 0; i < 8; i++) {
            SDL_Point p1 = pts[i];
            SDL_Point p2 = pts[(i+1)%8];
            if ((y >= p1.y && y < p2.y) || (y >= p2.y && y < p1.y)) {
                int dy = p2.y - p1.y;
                if (dy != 0) {
                    int ix = p1.x + (int)((float)(y - p1.y) * (float)(p2.x - p1.x) / (float)dy);
                    x[n++] = ix;
                }
            }
        }
        if (n < 2) continue;
        if (x[0] > x[1]) { int tmp = x[0]; x[0] = x[1]; x[1] = tmp; }
        SDL_RenderDrawLine(r, x[0], y, x[1], y);
    }
}

void AudioCallback(void* userdata, Uint8* stream, int len) {
    if (!audio_file) {
        SDL_memset(stream, 0, len);
        return;
    }
    size_t r = fread(stream, 1, len, audio_file);
    if (r < (size_t)len) {
        fseek(audio_file, 44, SEEK_SET);
        fread(stream + r, 1, len - r, audio_file);
    }
    int16_t* samples = (int16_t*)stream;
    int count = len / sizeof(int16_t);
    for (int i = 0; i < count; i++) {
        float v = samples[i] * MUSIC_VOLUME;
        if (v < -32768.f) v = -32768.f;
        else if (v > 32767.f) v = 32767.f;
        samples[i] = (int16_t)v;
    }
}

int main(void) {
    struct { int w, h, mode; } modes[] = {
        {640, 480, REFRESH_DEFAULT},
        {720, 480, REFRESH_DEFAULT},
    };
    bool found = false;
    for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i) {
        if (XVideoSetMode(modes[i].w, modes[i].h, 32, modes[i].mode) == TRUE) {
            screen_width = modes[i].w;
            screen_height = modes[i].h;
            found = true;
            break;
        }
    }
    if (!found) return 0;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        debugPrint("SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    if (TTF_Init() == -1) {
        debugPrint("TTF_Init failed: %s\n", TTF_GetError());
        return 0;
    }
    if ((IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) & (IMG_INIT_JPG | IMG_INIT_PNG)) == 0) {
        debugPrint("IMG_Init failed: %s\n", IMG_GetError());
        return 0;
    }
    if (nxNetInit(NULL) != 0) {
        debugPrint("Network initialization failed!\n");
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Type D Setup",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        screen_width, screen_height, SDL_WINDOW_SHOWN
    );
    if (!window) return 0;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_DestroyWindow(window);
        return 0;
    }

    SDL_Surface* bgSurface = IMG_Load("D:\\media\\img\\BG.jpg");
    if (!bgSurface) bgSurface = IMG_Load("D:\\media\\img\\BG.png");
    if (!bgSurface) bgSurface = SDL_LoadBMP("D:\\media\\img\\BG.bmp");
    SDL_Texture* bgTexture = bgSurface
        ? SDL_CreateTextureFromSurface(renderer, bgSurface)
        : NULL;
    if (bgSurface) SDL_FreeSurface(bgSurface);

    SDL_Surface* tdSurface = IMG_Load("D:\\media\\img\\TD.png");
    if (!tdSurface) tdSurface = IMG_Load("D:\\media\\img\\TD.jpg");
    if (!tdSurface) tdSurface = SDL_LoadBMP("D:\\media\\img\\TD.bmp");
    SDL_Texture* tdTexture = tdSurface
        ? SDL_CreateTextureFromSurface(renderer, tdSurface)
        : NULL;
    if (tdSurface) SDL_FreeSurface(tdSurface);

    int title_font_sz = screen_height / 18;
    TTF_Font* titleFont = TTF_OpenFont("D:\\media\\font\\font.ttf", title_font_sz);
    SDL_Texture* titleTex = NULL;
    SDL_Rect titleRect = {0};
    if (titleFont) {
        SDL_Surface* ts = TTF_RenderText_Blended(
            titleFont, "Type D Setup", (SDL_Color){255,255,255,255});
        if (ts) {
            titleTex = SDL_CreateTextureFromSurface(renderer, ts);
            titleRect.w = ts->w;
            titleRect.h = ts->h;
            titleRect.x = (screen_width - ts->w) / 2;
            titleRect.y = SCALEY(50);
            SDL_FreeSurface(ts);
        }
    }

    int exit_font_sz = screen_height / 32;
    TTF_Font* exitFont = TTF_OpenFont("D:\\media\\font\\font.ttf", exit_font_sz);

    SDL_Surface *exitLeft = NULL, *exitB = NULL, *exitRight = NULL;
    SDL_Texture *exitLeftTex = NULL, *exitBTex = NULL, *exitRightTex = NULL;
    SDL_Rect exitLeftRect = {0}, exitBRect = {0}, exitRightRect = {0};

    if (exitFont) {
        exitLeft = TTF_RenderText_Blended(exitFont, "Press ", (SDL_Color){220,220,220,255});
        exitB    = TTF_RenderText_Blended(exitFont, "B",      (SDL_Color){255,0,0,255});
        exitRight= TTF_RenderText_Blended(exitFont, " to exit", (SDL_Color){220,220,220,255});
        if (exitLeft && exitB && exitRight) {
            exitLeftTex = SDL_CreateTextureFromSurface(renderer, exitLeft);
            exitBTex    = SDL_CreateTextureFromSurface(renderer, exitB);
            exitRightTex= SDL_CreateTextureFromSurface(renderer, exitRight);

            int total_w = exitLeft->w + exitB->w + exitRight->w;
            int y = screen_height - exitLeft->h - SCALEY(20);
            int x = screen_width - total_w - SCALEX(20);

            exitLeftRect = (SDL_Rect){x, y, exitLeft->w, exitLeft->h};
            exitBRect    = (SDL_Rect){x + exitLeft->w, y, exitB->w, exitB->h};
            exitRightRect= (SDL_Rect){x + exitLeft->w + exitB->w, y, exitRight->w, exitRight->h};
        }
        if (exitLeft) SDL_FreeSurface(exitLeft);
        if (exitB)    SDL_FreeSurface(exitB);
        if (exitRight)SDL_FreeSurface(exitRight);
    }

    SDL_GameController* controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            break;
        }
    }

    audio_file = fopen("D:\\media\\snd\\BG.wav", "rb");
    if (audio_file) {
        static char buf[64*1024];
        setvbuf(audio_file, buf, _IOFBF, sizeof(buf));
        fseek(audio_file, 44, SEEK_SET);
        SDL_AudioSpec spec = {0};
        spec.freq = 44100;
        spec.format = AUDIO_S16LSB;
        spec.channels = 2;
        spec.samples = 2048;
        spec.callback = AudioCallback;
        if (SDL_OpenAudio(&spec, NULL) == 0) {
            SDL_PauseAudio(0);
        }
    }

    detect_start();

    // Prepare menu rectangles centered vertically
    SDL_Rect mrect[MENU_ITEM_COUNT];
    int cw = SCALEX(170), ch = SCALEY(56);
    int total_menu_height = MENU_ROWS * ch + (MENU_ROWS - 1) * SCALEY(20);
    int menu_start_y = (screen_height - total_menu_height) / 2;

    int cx[MENU_COLS] = {
        screen_width / 6,
        screen_width / 2,
        screen_width * 5 / 6
    };

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int col = i % MENU_COLS;
        int row = i / MENU_COLS;
        int px = cx[col] - cw / 2;
        int py = menu_start_y + row * (ch + SCALEY(20));
        // Center last row if not full
        if (row == MENU_ROWS - 1 && MENU_ITEM_COUNT % MENU_COLS != 0) {
            int remain = MENU_ITEM_COUNT % MENU_COLS;
            if (remain == 1)
                px = cx[1] - cw / 2;
            else if (remain == 2 && col == 0)
                px = cx[0] - cw / 2;
            else if (remain == 2 && col == 1)
                px = cx[1] - cw / 2;
        }
        mrect[i] = (SDL_Rect){ px, py, cw, ch };
    }

    SDL_Event event;
    bool running = true;

    while (running) {
#define DETECTED_MAX TYPE_D_MAX_UNITS
        type_d_unit_t detected[DETECTED_MAX] = {0};
        int n = detect_get_units(detected, DETECTED_MAX);
        int device_present[DEVICE_BAR_SLOTS] = {0};
        uint32_t device_ip[DEVICE_BAR_SLOTS] = {0};
        // Track if XL (ID 5) is present, store its IP for display
        bool xl_found = false;
        uint32_t xl_ip = 0;

        for (int i = 0; i < n; ++i) {
            int id = detected[i].id;
            if (id >= 1 && id <= DEVICE_MAX) {
                device_present[id-1] = 1;
                device_ip[id-1] = detected[i].ip;
            }
            if (id == 5) { // XL
                device_present[4] = 1;
                device_ip[4] = detected[i].ip;
                xl_found = true;
                xl_ip = detected[i].ip;
            }
        }

        // PATCH: For central menu "XL DETECTED"
        int xl_menu_idx = 4; // Center menu block (second row, second column)
        // PATCH: Check for ID 6 (EXP)
        bool exp_found = false;
        for (int i = 0; i < n; ++i) {
            if (detected[i].id == 6) {
                exp_found = true;
                break;
            }
        }

        // Auto-select first detected device if none selected (now up to 5 slots)
        if (selected_idx == -1) {
            for (int i = 0; i < DEVICE_BAR_SLOTS; i++) {
                if (device_present[i]) {
                    selected_idx = i;
                    break;
                }
            }
        }

        // Sticky selected_idx safeguard
        static int last_valid_selected_idx = -1;
        if (selected_idx >= 0 && selected_idx < DEVICE_BAR_SLOTS && device_present[selected_idx]) {
            last_valid_selected_idx = selected_idx;
        } else {
            if (last_valid_selected_idx >= 0 && device_present[last_valid_selected_idx]) {
                selected_idx = last_valid_selected_idx;
            } else {
                selected_idx = -1;
                last_valid_selected_idx = -1;
            }
        }

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;

            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    if (aboutVisible) {
                        aboutVisible = false;
                    } else {
                        running = false;
                    }
                } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    // Toggle About overlay on SELECT (BACK) button press
                    aboutVisible = !aboutVisible;
                } else if (!aboutVisible) {
                    // Only allow normal navigation when About overlay is NOT visible
                    if (focus_row == 0) {
                        // Menu navigation
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                            if (menu_cmds[menu_selected]) {
                                const char* target_ip = NULL;
                                if (selected_idx >= 0 && device_present[selected_idx])
                                    target_ip = detect_ipstr(device_ip[selected_idx]);
                                else if (n > 0)
                                    target_ip = detect_ipstr(detected[0].ip);
                                if (target_ip)
                                    send_cmd(target_ip, menu_cmds[menu_selected], NULL);
                            }
                        }
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                            int col = menu_selected % MENU_COLS;
                            int row = menu_selected / MENU_COLS;
                            if (col == 0) {
                                int last_in_row = (row + 1) * MENU_COLS - 1;
                                if (last_in_row >= MENU_ITEM_COUNT) last_in_row = MENU_ITEM_COUNT - 1;
                                menu_selected = last_in_row;
                            } else {
                                menu_selected--;
                            }
                        }
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                            int col = menu_selected % MENU_COLS;
                            int row = menu_selected / MENU_COLS;
                            int last_in_row = (row + 1) * MENU_COLS - 1;
                            if (last_in_row >= MENU_ITEM_COUNT) last_in_row = MENU_ITEM_COUNT - 1;
                            if (col == MENU_COLS - 1 || menu_selected == last_in_row) {
                                menu_selected = row * MENU_COLS;
                            } else {
                                menu_selected++;
                            }
                        }
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                            int col = menu_selected % MENU_COLS;
                            int row = menu_selected / MENU_COLS;
                            if (row == 0) {
                                int last_row = (MENU_ITEM_COUNT - 1) / MENU_COLS;
                                int dest = last_row * MENU_COLS + col;
                                if (dest >= MENU_ITEM_COUNT) dest = MENU_ITEM_COUNT - 1;
                                menu_selected = dest;
                            } else {
                                menu_selected -= MENU_COLS;
                            }
                        }
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                            int col = menu_selected % MENU_COLS;
                            int row = menu_selected / MENU_COLS;
                            int last_row = (MENU_ITEM_COUNT - 1) / MENU_COLS;

                            if (row == last_row) {
                                // Move focus to device bar only if already on last menu row
                                focus_row = 1;
                            } else {
                                // Move down normally in menu
                                int dest = menu_selected + MENU_COLS;
                                if (dest >= MENU_ITEM_COUNT) {
                                    dest = (last_row * MENU_COLS) + col;
                                    if (dest >= MENU_ITEM_COUNT)
                                        dest = MENU_ITEM_COUNT - 1;
                                }
                                menu_selected = dest;
                            }
                        }
                    } else if (focus_row == 1) {
                        // Device bar navigation
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                            focus_row = 0;
                        }
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                            int orig = highlight_idx;
                            do {
                                highlight_idx = (highlight_idx - 1 + DEVICE_BAR_SLOTS) % DEVICE_BAR_SLOTS;
                            } while (!device_present[highlight_idx] && highlight_idx != orig);
                        }
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                            int orig = highlight_idx;
                            do {
                                highlight_idx = (highlight_idx + 1) % DEVICE_BAR_SLOTS;
                            } while (!device_present[highlight_idx] && highlight_idx != orig);
                        }
                        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                            if (device_present[highlight_idx]) {
                                selected_idx = highlight_idx;
                            }
                        }
                    }
                }
            }
        }

        SDL_RenderClear(renderer);
        if (bgTexture) SDL_RenderCopy(renderer, bgTexture, NULL, NULL);

        if (aboutVisible) {
            // ...about overlay code unchanged...
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
            SDL_Rect overlayRect = {SCALEX(40), SCALEY(80), screen_width - SCALEX(80), screen_height - SCALEY(160)};
            SDL_RenderFillRect(renderer, &overlayRect);

            // About text lines
            SDL_Color white = {255, 255, 255, 255};
            const char* aboutLines[] = {
                "Type D Setup",
                "Code By: Darkone83",
                "Music: Minus Eleven",
                "By: La Castle Vania",
                "Press Back to close"
            };

            // Calculate total height of text block including extra spacing
            int lineHeights[sizeof(aboutLines)/sizeof(aboutLines[0])] = {0};
            int totalHeight = 0;
            for (int i = 0; i < (int)(sizeof(aboutLines)/sizeof(aboutLines[0])); i++) {
                int w, h;
                TTF_SizeText(titleFont, aboutLines[i], &w, &h);
                lineHeights[i] = h;
                totalHeight += h;
                if (i == 0 || i == 1) {
                    totalHeight += SCALEY(20);
                } else if (i == (int)(sizeof(aboutLines)/sizeof(aboutLines[0])) - 2) {
                    totalHeight += SCALEY(30);
                } else {
                    totalHeight += SCALEY(10);
                }
            }

            // Starting Y to vertically center block inside overlayRect
            int y = overlayRect.y + (overlayRect.h - totalHeight) / 2;

            // Render each line
            for (int i = 0; i < (int)(sizeof(aboutLines)/sizeof(aboutLines[0])); i++) {
                SDL_Surface* surf = TTF_RenderText_Blended(titleFont, aboutLines[i], white);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                    if (tex) {
                        SDL_Rect rect;
                        rect.w = surf->w;
                        rect.h = surf->h;
                        rect.x = overlayRect.x + (overlayRect.w - surf->w) / 2; // horizontally center
                        rect.y = y;
                        SDL_RenderCopy(renderer, tex, NULL, &rect);
                        SDL_DestroyTexture(tex);
                    }
                    y += lineHeights[i];
                    if (i == 0 || i == 1) {
                        y += SCALEY(20);
                    } else if (i == (int)(sizeof(aboutLines)/sizeof(aboutLines[0])) - 2) {
                        y += SCALEY(30);
                    } else {
                        y += SCALEY(10);
                    }
                    SDL_FreeSurface(surf);
                }
            }
        } else {
            // Draw menu buttons
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                SDL_Rect rect = mrect[i];
                SDL_Rect shadow = rect;
                shadow.x += SCALEX(8);
                shadow.y += SCALEY(8);

                fill_octagon(renderer, shadow, SCALEY(12), (SDL_Color){0,0,0,80});
                if (focus_row == 0 && i == menu_selected)
                    fill_octagon(renderer, rect, SCALEY(12), (SDL_Color){0,220,0,255});
                else
                    fill_octagon(renderer, rect, SCALEY(12), (SDL_Color){36,36,36,255});
                draw_octagon(renderer, rect, SCALEY(12), (SDL_Color){80,255,100,255});

                // PATCH: XL DETECTED in center menu block
                if (i == xl_menu_idx && xl_found) {
                    SDL_Surface* xlSurf = TTF_RenderText_Blended(exitFont ? exitFont : titleFont, "XL DETECTED", (SDL_Color){220,30,180,255});
                    if (xlSurf) {
                        SDL_Texture* xlTex = SDL_CreateTextureFromSurface(renderer, xlSurf);
                        if (xlTex) {
                            SDL_Rect textRect = rect;
                            textRect.x += (rect.w - xlSurf->w) / 2;
                            textRect.y += (rect.h - xlSurf->h) / 2;
                            textRect.w = xlSurf->w;
                            textRect.h = xlSurf->h;
                            SDL_RenderCopy(renderer, xlTex, NULL, &textRect);
                            SDL_DestroyTexture(xlTex);
                        }
                        SDL_FreeSurface(xlSurf);
                    }
                    continue;
                }

                SDL_Color textColor = (focus_row == 0 && i == menu_selected)
                    ? (SDL_Color){0,0,0,255}
                    : (SDL_Color){255,255,255,255};
                SDL_Surface* ms = TTF_RenderText_Blended(
                    exitFont ? exitFont : titleFont, menu_items[i], textColor);
                if (!ms) continue;
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, ms);
                if (!tx) {
                    SDL_FreeSurface(ms);
                    continue;
                }
                SDL_Rect textRect = rect;
                textRect.x += (rect.w - ms->w) / 2;
                textRect.y += (rect.h - ms->h) / 2;
                textRect.w = ms->w;
                textRect.h = ms->h;
                SDL_FreeSurface(ms);
                SDL_RenderCopy(renderer, tx, NULL, &textRect);
                SDL_DestroyTexture(tx);
            }

            // Draw TD logo and title
            if (tdTexture) {
                int logo_w = SCALEX(64);
                int logo_h = SCALEY(64);
                SDL_Rect logoRect = {SCALEX(24), SCALEY(24), logo_w, logo_h};
                SDL_RenderCopy(renderer, tdTexture, NULL, &logoRect);
            }
            if (titleTex) SDL_RenderCopy(renderer, titleTex, NULL, &titleRect);

            // Draw device selection bar
            int x0 = SCALEX(20);
            int y0 = screen_height - SCALEY(70);
            if (exitFont) {
                SDL_Surface* label_surf = TTF_RenderText_Blended(exitFont, "Available Type D units:", (SDL_Color){200,200,200,255});
                if (label_surf) {
                    SDL_Texture* label_tex = SDL_CreateTextureFromSurface(renderer, label_surf);
                    if (label_tex) {
                        SDL_Rect label_rect = {x0, y0, label_surf->w, label_surf->h};
                        SDL_RenderCopy(renderer, label_tex, NULL, &label_rect);
                        SDL_DestroyTexture(label_tex);
                    }
                    SDL_FreeSurface(label_surf);
                }
                int spacing = SCALEX(32);
                int num_y = y0 + TTF_FontHeight(exitFont) + 4;
                for (int i = 0; i < DEVICE_BAR_SLOTS; ++i) {
                    SDL_Color color = {128,128,128,255};
                    SDL_Color border = {80,255,100,255};
                    const char* label = NULL;
                    if (i < DEVICE_MAX)
                        label = (const char*[]){"1","2","3","4"}[i];
                    else
                        label = "XL";
                    if (device_present[i]) {
                        if (i == selected_idx)
                            color = (SDL_Color){0,128,0,255};
                        else if (focus_row == 1 && i == highlight_idx)
                            color = (SDL_Color){0,255,128,255};
                        else
                            color = (SDL_Color){255,255,255,255};
                    }
                    SDL_Surface* nsurf = TTF_RenderText_Blended(exitFont, label, color);
                    if (nsurf) {
                        SDL_Texture* ntex = SDL_CreateTextureFromSurface(renderer, nsurf);
                        if (ntex) {
                            SDL_Rect nrect = {x0 + i*spacing, num_y, nsurf->w, nsurf->h};
                            if (focus_row == 1 && i == highlight_idx && device_present[i]) {
                                SDL_Rect box = nrect;
                                box.x -= 2; box.y -= 2; box.w += 4; box.h += 4;
                                SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
                                SDL_RenderDrawRect(renderer, &box);
                            }
                            SDL_RenderCopy(renderer, ntex, NULL, &nrect);
                            SDL_DestroyTexture(ntex);
                        }
                        SDL_FreeSurface(nsurf);
                    }
                }
                int info_y = num_y + TTF_FontHeight(exitFont) + 4;
                if (selected_idx >= 0 && device_present[selected_idx]) {
                    char ipmsg[80];
                    if (selected_idx == 4) {
                        snprintf(ipmsg, sizeof(ipmsg), "Type D XL IP: %s", detect_ipstr(device_ip[4]));
                    } else {
                        snprintf(ipmsg, sizeof(ipmsg), "Type D IP: %s", detect_ipstr(device_ip[selected_idx]));
                    }
                    SDL_Surface* ipsurf = TTF_RenderText_Blended(exitFont, ipmsg, (SDL_Color){200,200,200,255});
                    if (ipsurf) {
                        SDL_Texture* iptex = SDL_CreateTextureFromSurface(renderer, ipsurf);
                        if (iptex) {
                            SDL_Rect iprect = {x0, info_y, ipsurf->w, ipsurf->h};
                            SDL_RenderCopy(renderer, iptex, NULL, &iprect);
                            SDL_DestroyTexture(iptex);
                        }
                        SDL_FreeSurface(ipsurf);
                    }
                }
            }

            // Draw exit prompt
            if (exitLeftTex) SDL_RenderCopy(renderer, exitLeftTex, NULL, &exitLeftRect);
            if (exitBTex) SDL_RenderCopy(renderer, exitBTex, NULL, &exitBRect);
            if (exitRightTex) SDL_RenderCopy(renderer, exitRightTex, NULL, &exitRightRect);

            // Draw EXP indicator if ID 6 is detected (unchanged)
            if (exp_found && exitFont) {
                SDL_Surface* expSurf = TTF_RenderText_Blended(exitFont, "EXP FOUND", (SDL_Color){0,255,80,255});
                if (expSurf) {
                    SDL_Texture* expTex = SDL_CreateTextureFromSurface(renderer, expSurf);
                    if (expTex) {
                        int margin = SCALEY(16);
                        SDL_Rect expRect;
                        expRect.w = expSurf->w;
                        expRect.h = expSurf->h;
                        expRect.x = screen_width - expRect.w - margin;
                        expRect.y = margin;
                        SDL_RenderCopy(renderer, expTex, NULL, &expRect);
                        SDL_DestroyTexture(expTex);
                    }
                    SDL_FreeSurface(expSurf);
                }
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_CloseAudio();
    if (audio_file) fclose(audio_file);
    if (bgTexture) SDL_DestroyTexture(bgTexture);
    if (tdTexture) SDL_DestroyTexture(tdTexture);
    if (titleTex) SDL_DestroyTexture(titleTex);
    if (titleFont) TTF_CloseFont(titleFont);
    if (exitLeftTex) SDL_DestroyTexture(exitLeftTex);
    if (exitBTex) SDL_DestroyTexture(exitBTex);
    if (exitRightTex) SDL_DestroyTexture(exitRightTex);
    if (exitFont) TTF_CloseFont(exitFont);
    if (controller) SDL_GameControllerClose(controller);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);

    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    return 0;
}
