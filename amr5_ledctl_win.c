#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_VERSION "0.1"

#define INDEX_PORT 0x4E
#define DATA_PORT  0x4F

#define REG_MODE   0x4BE
#define REG_PARAM1 0x45B
#define REG_PARAM2 0x45C

typedef void(__stdcall* Out32Func)(short port, short data);
typedef short(__stdcall* Inp32Func)(short port);
typedef int(__stdcall* IsInpOutDriverOpenFunc)(void);

typedef struct IoDll {
    HMODULE module;
    Out32Func Out32;
    Inp32Func Inp32;
    IsInpOutDriverOpenFunc IsInpOutDriverOpen;
} IoDll;

typedef enum Mode {
    MODE_INVALID = -1,
    MODE_OFF,
    MODE_BUTTON,
    MODE_STATIC,
    MODE_DEFAULT,
    MODE_RAINBOW,
    MODE_BREATH,
    MODE_CYCLE
} Mode;

void error_msg(const char* fmt, const char* arg) {
    char buf[512];
    snprintf(buf, sizeof(buf), fmt, arg);
    MessageBoxA(NULL, buf, "AMR5 LED Error", MB_OK | MB_ICONERROR);
}

void attach_console_if_present(void) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
}

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s <mode> [--dll PATH] [--delay MS]\n\n"
        "Modes:\n"
        "  off         all LEDs off\n"
        "  button      only main button\n"
        "  static      all zones static\n"
        "  default     rainbow + button\n"
        "  rainbow     all zones rainbow\n"
        "  breath      all zones breathing\n"
        "  cycle       all zones cycle\n",
        prog);
}

static Mode parse_mode(const char* s) {
    if (strcmp(s, "off") == 0) return MODE_OFF;
    if (strcmp(s, "button") == 0) return MODE_BUTTON;
    if (strcmp(s, "static") == 0) return MODE_STATIC;
    if (strcmp(s, "default") == 0) return MODE_DEFAULT;
    if (strcmp(s, "rainbow") == 0) return MODE_RAINBOW;
    if (strcmp(s, "breath") == 0) return MODE_BREATH;
    if (strcmp(s, "cycle") == 0) return MODE_CYCLE;
    return MODE_INVALID;
}

static int load_io_dll(const char* dll_path, IoDll* io) {
    memset(io, 0, sizeof(*io));
    io->module = LoadLibraryA(dll_path);
    if (!io->module) {
        fprintf(stderr, "Failed to load DLL: %s\n", dll_path);
        error_msg(" Failed to load DLL:\n %s", dll_path);
        return 0;
    }

    io->Out32 = (Out32Func)GetProcAddress(io->module, "Out32");
    io->Inp32 = (Inp32Func)GetProcAddress(io->module, "Inp32");
    io->IsInpOutDriverOpen = (IsInpOutDriverOpenFunc)GetProcAddress(io->module, "IsInpOutDriverOpen");

    if (!io->Out32 || !io->Inp32 || !io->IsInpOutDriverOpen) {
        fprintf(stderr, "Missing required exports in %s\n", dll_path);
        FreeLibrary(io->module);
        memset(io, 0, sizeof(*io));
        return 0;
    }

    if (!io->IsInpOutDriverOpen()) {
        fprintf(stderr, "InpOut driver is not open. Run as Administrator and ensure inpoutx64.dll can load its driver.\n");
        FreeLibrary(io->module);
        memset(io, 0, sizeof(*io));
        return 0;
    }

    return 1;
}

static void unload_io_dll(IoDll* io) {
    if (io->module) {
        FreeLibrary(io->module);
        memset(io, 0, sizeof(*io));
    }
}

static void write8(IoDll* io, unsigned short port, unsigned char value) {
    io->Out32((short)port, (short)value);
}

static void write_reg(IoDll* io, unsigned short reg, unsigned char value) {
    unsigned char reg_high = (unsigned char)((reg >> 8) & 0xFF);
    unsigned char reg_low = (unsigned char)(reg & 0xFF);

    write8(io, INDEX_PORT, 0x2E);
    write8(io, DATA_PORT, 0x11);
    write8(io, INDEX_PORT, 0x2F);
    write8(io, DATA_PORT, reg_high);

    write8(io, INDEX_PORT, 0x2E);
    write8(io, DATA_PORT, 0x10);
    write8(io, INDEX_PORT, 0x2F);
    write8(io, DATA_PORT, reg_low);

    write8(io, INDEX_PORT, 0x2E);
    write8(io, DATA_PORT, 0x12);
    write8(io, INDEX_PORT, 0x2F);
    write8(io, DATA_PORT, value);
}

static void apply_mode(IoDll* io, Mode mode, DWORD delay_ms) {
    write_reg(io, REG_PARAM2, 0x00);

    switch (mode) {
    case MODE_STATIC:
        write_reg(io, REG_MODE, 0x02);
        Sleep(delay_ms);
        write_reg(io, REG_MODE, 0x03);
        return;

    case MODE_BUTTON:
        write_reg(io, REG_MODE, 0x07);
        Sleep(delay_ms);
        write_reg(io, REG_MODE, 0x03);
        return;

    case MODE_RAINBOW:
        write_reg(io, REG_PARAM1, 0x32);
        write_reg(io, REG_PARAM2, 0x07);
        write_reg(io, REG_MODE, 0x05);
        return;

    case MODE_OFF:
        write_reg(io, REG_MODE, 0x07);
        return;

    case MODE_DEFAULT:
        write_reg(io, REG_MODE, 0x01);
        return;

    case MODE_BREATH:
        write_reg(io, REG_MODE, 0x02);
        return;

    case MODE_CYCLE:
        write_reg(io, REG_MODE, 0x06);
        return;

    default:
        return;
    }
}

int real_main(int argc, char** argv) {
    const char* dll_path = "inpoutx64.dll";
    DWORD delay_ms = 10;
    Mode mode;
    IoDll io;
    int i;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("amr5_ledctl_win %s\n", APP_VERSION);
        return 0;
    }

    mode = parse_mode(argv[1]);
    if (mode == MODE_INVALID) {
        usage(argv[0]);
        return 1;
    }

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("amr5_ledctl_win %s\n", APP_VERSION);
            return 0;
        }
        else if (strcmp(argv[i], "--dll") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--dll requires a path\n");
                return 1;
            }
            dll_path = argv[++i];
        }
        else if (strcmp(argv[i], "--delay") == 0) {
            char* end = NULL;
            unsigned long value;
            if (i + 1 >= argc) {
                fprintf(stderr, "--delay requires milliseconds\n");
                return 1;
            }
            value = strtoul(argv[++i], &end, 10);
            if (!end || *end != '\0') {
                fprintf(stderr, "Invalid delay: %s\n", argv[i]);
                return 1;
            }
            delay_ms = (DWORD)value;
        }
        else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!load_io_dll(dll_path, &io)) {
        return 2;
    }

    apply_mode(&io, mode, delay_ms);
    unload_io_dll(&io);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    attach_console_if_present();
    return real_main(__argc, __argv);
}
