// Simulador de Urna Eletronica para PS Vita (VitaSDK + vita2d)
// Candidatos FICTICIOS. Uso educacional.
#include <psp2/ctrl/ctrl.h>
#include <psp2/touch.h>
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define RGB(r,g,b) RGBA8(r,g,b,255)
#define C_BG     RGB(30,30,30)
#define C_SCREEN RGB(235,235,225)
#define C_KEY    RGB(60,60,60)
#define C_KEYHI  RGB(110,110,110)
#define C_TEXT   RGB(20,20,20)

typedef struct { const char *num, *nome, *partido; unsigned int cor; } Candidato;

static const Candidato CANDS[] = {
    {"13", "Ana Exemplo",    "Partido Azul",     RGB( 40,100,200)},
    {"22", "Bruno Teste",    "Partido Verde",    RGB( 40,160, 80)},
    {"45", "Carla Ficticia", "Partido Amarelo",  RGB(210,170, 30)},
    {"77", "Davi Demo",      "Partido Vermelho", RGB(200, 60, 60)},
};
#define NCANDS (int)(sizeof(CANDS)/sizeof(CANDS[0]))

enum { A_NONE = -1, A_BRANCO = 10, A_CORRIGE = 11, A_CONFIRMA = 12 };
enum { ST_VOTANDO, ST_FIM };

typedef struct { int x, y, w, h, action; const char *label; unsigned int cor; } Btn;
static Btn btns[13];

static void build_buttons(void) {
    static const char *lbl[] = {"1","2","3","4","5","6","7","8","9","","0",""};
    int k = 0;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++, k++) {
            int digit = (r < 3) ? r*3 + c + 1 : (c == 1 ? 0 : -1);
            if (digit < 0) continue;
            btns[digit] = (Btn){620 + c*110, 30 + r*80, 100, 70, digit, lbl[k], C_KEY};
        }
    btns[10] = (Btn){620, 400, 100, 90, A_BRANCO,   "BRANCO",   RGB(235,235,235)};
    btns[11] = (Btn){730, 400, 100, 90, A_CORRIGE,  "CORRIGE",  RGB(230,110, 30)};
    btns[12] = (Btn){840, 400, 100, 90, A_CONFIRMA, "CONFIRMA", RGB( 40,170, 70)};
}

static int find_cand(const char *num) {
    for (int i = 0; i < NCANDS; i++) if (!strcmp(CANDS[i].num, num)) return i;
    return -1;
}

// ---- Som de "FIM" (thread para nao travar a tela) ----
static int beep_thread(SceSize args, void *argp) {
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_MONO);
    if (port >= 0) {
        static short buf[1024];
        double phase = 0, step = 2.0 * M_PI * 1000.0 / 48000.0; // 1 kHz
        for (int n = 0; n < 48000 * 2 / 1024; n++) {
            for (int i = 0; i < 1024; i++) { buf[i] = (short)(sin(phase) * 9000); phase += step; }
            sceAudioOutOutput(port, buf);
        }
        sceAudioOutOutput(port, NULL); // espera terminar
        sceAudioOutReleasePort(port);
    }
    return sceKernelExitDeleteThread(0);
}
static void play_fim(void) {
    SceUID t = sceKernelCreateThread("beep", beep_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

int main(void) {
    vita2d_init();
    vita2d_set_clear_color(C_BG);
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    build_buttons();

    char digits[3] = "";
    int n = 0, branco = 0, estado = ST_VOTANDO, total_votos = 0;
    unsigned int fim_tick = 0, frame = 0;
    unsigned int prev_btn = 0; int touch_was = 0, pressed = -1;

    for (;;) {
        // ---- Entrada ----
        int action = A_NONE;
        SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int down = pad.buttons & ~prev_btn; prev_btn = pad.buttons;
        if (down & SCE_CTRL_CROSS)  action = A_CONFIRMA;
        if (down & SCE_CTRL_CIRCLE) action = A_CORRIGE;
        if (down & SCE_CTRL_SQUARE) action = A_BRANCO;
        if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT)) break;

        SceTouchData t; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1);
        int is_down = t.reportNum > 0, tx = -1, ty = -1;
        if (is_down) { tx = t.report[0].x / 2; ty = t.report[0].y / 2; } // 1920x1088 -> 960x544
        pressed = -1;
        if (is_down)
            for (int i = 0; i < 13; i++)
                if (tx >= btns[i].x && tx < btns[i].x + btns[i].w &&
                    ty >= btns[i].y && ty < btns[i].y + btns[i].h) pressed = i;
        if (is_down && !touch_was && pressed >= 0) action = pressed;
        touch_was = is_down;

        // ---- Logica ----
        frame++;
        if (estado == ST_VOTANDO && action != A_NONE) {
            if (action <= 9) {
                if (!branco && n < 2) { digits[n++] = '0' + action; digits[n] = 0; }
            } else if (action == A_BRANCO) {
                if (n == 0) branco = 1;
            } else if (action == A_CORRIGE) {
                n = 0; digits[0] = 0; branco = 0;
            } else if (action == A_CONFIRMA) {
                if (branco || n == 2) {
                    total_votos++;
                    estado = ST_FIM; fim_tick = frame; play_fim();
                }
            }
        }
        if (estado == ST_FIM && frame - fim_tick > 60 * 3) { // ~3 s
            n = 0; digits[0] = 0; branco = 0; estado = ST_VOTANDO;
        }

        // ---- Desenho ----
        vita2d_start_drawing();
        vita2d_clear_screen();

        // Tela da urna
        vita2d_draw_rectangle(20, 20, 580, 504, C_SCREEN);

        if (estado == ST_FIM) {
            const char *s = "FIM";
            float sc = 4.0f;
            int w = vita2d_pgf_text_width(pgf, sc, s);
            vita2d_pgf_draw_text(pgf, 20 + (580 - w)/2, 300, C_TEXT, sc, s);
            vita2d_pgf_draw_text(pgf, 200, 380, C_TEXT, 1.3f, "VOTOU");
        } else {
            vita2d_pgf_draw_text(pgf, 40, 65, C_TEXT, 1.0f, "SEU VOTO PARA");
            vita2d_pgf_draw_text(pgf, 40, 120, C_TEXT, 1.6f, "PRESIDENTE");

            if (branco) {
                vita2d_pgf_draw_text(pgf, 120, 270, C_TEXT, 2.4f, "VOTO EM BRANCO");
            } else {
                vita2d_pgf_draw_text(pgf, 40, 190, C_TEXT, 1.0f, "NÚMERO:");
                for (int i = 0; i < 2; i++) {
                    int bx = 150 + i*70;
                    vita2d_draw_rectangle(bx, 150, 56, 66, C_TEXT);
                    vita2d_draw_rectangle(bx+3, 153, 50, 60, C_SCREEN);
                    if (i < n) {
                        char d[2] = {digits[i], 0};
                        vita2d_pgf_draw_text(pgf, bx + 14, 202, C_TEXT, 2.0f, d);
                    } else if (i == n && (frame / 30) % 2 == 0) {
                        vita2d_draw_rectangle(bx + 12, 205, 32, 4, C_TEXT);
                    }
                }
                if (n == 2) {
                    int c = find_cand(digits);
                    if (c >= 0) {
                        vita2d_draw_rectangle(400, 140, 170, 200, CANDS[c].cor); // "foto"
                        vita2d_pgf_draw_text(pgf, 40, 270, C_TEXT, 1.0f, "Nome:");
                        vita2d_pgf_draw_text(pgf, 40, 305, C_TEXT, 1.4f, CANDS[c].nome);
                        vita2d_pgf_draw_text(pgf, 40, 350, C_TEXT, 1.0f, "Partido:");
                        vita2d_pgf_draw_text(pgf, 40, 385, C_TEXT, 1.4f, CANDS[c].partido);
                    } else {
                        vita2d_pgf_draw_text(pgf, 40, 290, C_TEXT, 1.5f, "NÚMERO ERRADO");
                        vita2d_pgf_draw_text(pgf, 40, 340, C_TEXT, 1.8f, "VOTO NULO");
                    }
                }
            }
            if (branco || n == 2) {
                vita2d_draw_line(40, 440, 580, 440, C_TEXT);
                vita2d_pgf_draw_text(pgf, 40, 470, C_TEXT, 0.9f, "Aperte a tecla:");
                vita2d_pgf_draw_text(pgf, 40, 495, C_TEXT, 0.9f, "VERDE para CONFIRMAR este voto");
                vita2d_pgf_draw_text(pgf, 40, 517, C_TEXT, 0.9f, "LARANJA para REINICIAR este voto");
            }
        }

        // Teclado
        for (int i = 0; i < 13; i++) {
            Btn *b = &btns[i];
            unsigned int cor = (pressed == i) ? C_KEYHI : b->cor;
            vita2d_draw_rectangle(b->x, b->y, b->w, b->h, cor);
            unsigned int tc = (i <= 9) ? RGB(255,255,255) : C_TEXT;
            float sc = (i <= 9) ? 1.8f : 0.8f;
            int w = vita2d_pgf_text_width(pgf, sc, b->label);
            vita2d_pgf_draw_text(pgf, b->x + (b->w - w)/2, b->y + b->h/2 + (i <= 9 ? 14 : 8), tc, sc, b->label);
        }

        char info[64]; snprintf(info, sizeof info, "Votos: %d  |  X=confirma O=corrige []=branco", total_votos);
        vita2d_pgf_draw_text(pgf, 620, 520, RGB(180,180,180), 0.6f, info);

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_free_pgf(pgf);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
