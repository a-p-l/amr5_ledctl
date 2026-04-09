#include <windows.h>
#include <winsvc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_VERSION "0.1"

#define INDEX_PORT 0x4E
#define DATA_PORT  0x4F

#define REG_MODE   0x4BE
#define REG_PARAM1 0x45B
#define REG_PARAM2 0x45C

#define DRIVER_NAME_X86  "inpout32"
#define DRIVER_NAME_X64  "inpoutx64"
#define DEFAULT_X86_SYS  "inpout32.sys"
#define DEFAULT_X64_SYS  "inpoutx64.sys"

#define IOCTL_READ_PORT_UCHAR   CTL_CODE(40000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE_PORT_UCHAR  CTL_CODE(40000, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

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

typedef struct IoDriver {
    HANDLE handle;
    char service_name[32];
    char device_name[64];
    char driver_path[MAX_PATH];
} IoDriver;

void error_msg(const char *fmt, ...) {
    char buf[512];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    MessageBoxA(NULL, buf, "AMR5 LED Error", MB_OK | MB_ICONERROR);
    fprintf(stderr, "%s\n", buf);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <mode> [--driver PATH] [--delay MS]\n\n"
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

static Mode parse_mode(const char *s) {
    if (strcmp(s, "off") == 0) return MODE_OFF;
    if (strcmp(s, "button") == 0) return MODE_BUTTON;
    if (strcmp(s, "static") == 0) return MODE_STATIC;
    if (strcmp(s, "default") == 0) return MODE_DEFAULT;
    if (strcmp(s, "rainbow") == 0) return MODE_RAINBOW;
    if (strcmp(s, "breath") == 0) return MODE_BREATH;
    if (strcmp(s, "cycle") == 0) return MODE_CYCLE;
    return MODE_INVALID;
}

static int is_x64_os(void) {
#if defined(_M_X64) || defined(__x86_64__)
    return 1;
#else
    BOOL wow64 = FALSE;
    typedef BOOL (WINAPI *IsWow64ProcessFunc)(HANDLE, PBOOL);
    IsWow64ProcessFunc fn;

    fn = (IsWow64ProcessFunc)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process");
    if (!fn) {
        return 0;
    }
    if (!fn(GetCurrentProcess(), &wow64)) {
        return 0;
    }
    return wow64 ? 1 : 0;
#endif
}

static int file_exists(const char *path) {
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static int copy_file_to_system_driver_dir(const char *source_path, const char *service_name) {
    char system_dir[MAX_PATH];
    char target_path[MAX_PATH];
    size_t name_len;
    PVOID old_value = NULL;
    typedef BOOL (WINAPI *DisableWow64FsRedirectionFunc)(PVOID *);
    typedef BOOL (WINAPI *RevertWow64FsRedirectionFunc)(PVOID);
    DisableWow64FsRedirectionFunc disable_redirection;
    RevertWow64FsRedirectionFunc revert_redirection;

    if (!GetSystemDirectoryA(system_dir, MAX_PATH)) {
        error_msg("GetSystemDirectory failed: %lu", GetLastError());
        return 0;
    }

    if (strcat_s(system_dir, MAX_PATH, "\\drivers\\") != 0) {
        error_msg("%s", "Failed to build driver directory path");
        return 0;
    }

    name_len = strlen(service_name);
    if (name_len + 4 >= (MAX_PATH - strlen(system_dir))) {
        error_msg("%s", "Driver service name is too long");
        return 0;
    }

    strcpy_s(target_path, MAX_PATH, system_dir);
    strcat_s(target_path, MAX_PATH, service_name);
    strcat_s(target_path, MAX_PATH, ".sys");

    disable_redirection = (DisableWow64FsRedirectionFunc)GetProcAddress(
        GetModuleHandleA("kernel32.dll"),
        "Wow64DisableWow64FsRedirection");
    revert_redirection = (RevertWow64FsRedirectionFunc)GetProcAddress(
        GetModuleHandleA("kernel32.dll"),
        "Wow64RevertWow64FsRedirection");

    if (!is_x64_os() || sizeof(void *) == 8 || !disable_redirection || !revert_redirection) {
        old_value = NULL;
    } else if (!disable_redirection(&old_value)) {
        old_value = NULL;
    }

    if (!CopyFileA(source_path, target_path, FALSE)) {
        DWORD error = GetLastError();
        if (old_value && revert_redirection) {
            revert_redirection(old_value);
        }
        error_msg("CopyFile failed: %lu", error);
        return 0;
    }

    if (old_value && revert_redirection) {
        revert_redirection(old_value);
    }

    return 1;
}

static DWORD ensure_service_running(const char *driver_path, const char *service_name) {
    SC_HANDLE scm = NULL;
    SC_HANDLE service = NULL;
    char nt_path[MAX_PATH];
    DWORD last_error = ERROR_GEN_FAILURE;
    SERVICE_STATUS status;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        last_error = GetLastError();
        error_msg("OpenSCManager failed: %lu", last_error);
        return last_error;
    }

    service = OpenServiceA(scm, service_name, SERVICE_ALL_ACCESS);
    if (!service) {
        last_error = GetLastError();
        if (last_error != ERROR_SERVICE_DOES_NOT_EXIST) {
            error_msg("OpenService failed: %lu", last_error);
            CloseServiceHandle(scm);
            return last_error;
        }

        if (!file_exists(driver_path)) {
            error_msg("Driver file not found: %s", driver_path);
            CloseServiceHandle(scm);
            return ERROR_FILE_NOT_FOUND;
        }

        if (!copy_file_to_system_driver_dir(driver_path, service_name)) {
            CloseServiceHandle(scm);
            return GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        }

        if (!GetSystemDirectoryA(nt_path, MAX_PATH)) {
            last_error = GetLastError();
            error_msg("GetSystemDirectory failed: %lu", last_error);
            CloseServiceHandle(scm);
            return last_error;
        }

        if (strcat_s(nt_path, MAX_PATH, "\\drivers\\") != 0 ||
            strcat_s(nt_path, MAX_PATH, service_name) != 0 ||
            strcat_s(nt_path, MAX_PATH, ".sys") != 0) {
            error_msg("%s", "Failed to build service binary path");
            CloseServiceHandle(scm);
            return ERROR_BUFFER_OVERFLOW;
        }

        service = CreateServiceA(
            scm,
            service_name,
            service_name,
            SERVICE_ALL_ACCESS,
            SERVICE_KERNEL_DRIVER,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            nt_path,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL);

        if (!service) {
            last_error = GetLastError();
            if (last_error != ERROR_SERVICE_EXISTS) {
                error_msg("CreateService failed: %lu", last_error);
                CloseServiceHandle(scm);
                return last_error;
            }
            service = OpenServiceA(scm, service_name, SERVICE_ALL_ACCESS);
            if (!service) {
                last_error = GetLastError();
                error_msg("OpenService after ERROR_SERVICE_EXISTS failed: %lu", last_error);
                CloseServiceHandle(scm);
                return last_error;
            }
        }
    }

    if (!StartService(service, 0, NULL)) {
        last_error = GetLastError();
        if (last_error != ERROR_SERVICE_ALREADY_RUNNING) {
            if (QueryServiceStatus(service, &status) &&
                status.dwCurrentState == SERVICE_RUNNING) {
                last_error = ERROR_SUCCESS;
            } else {
                error_msg("StartService failed: %lu", last_error);
            }
        } else {
            last_error = ERROR_SUCCESS;
        }
    } else {
        last_error = ERROR_SUCCESS;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return last_error;
}

static int open_driver(IoDriver *io, const char *driver_path) {
    const char *service_name = is_x64_os() ? DRIVER_NAME_X64 : DRIVER_NAME_X86;
    DWORD error;

    memset(io, 0, sizeof(*io));
    strcpy_s(io->service_name, sizeof(io->service_name), service_name);
    sprintf_s(io->device_name, sizeof(io->device_name), "\\\\.\\%s", service_name);
    strncpy_s(io->driver_path, sizeof(io->driver_path), driver_path, _TRUNCATE);

    io->handle = CreateFile(
        io->device_name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (io->handle != INVALID_HANDLE_VALUE) {
        return 1;
    }

    error = ensure_service_running(driver_path, service_name);
    if (error != ERROR_SUCCESS) {
        error_msg("Unable to install/start %s driver. Run as Administrator.", service_name);
        io->handle = NULL;
        return 0;
    }

    io->handle = CreateFile(
        io->device_name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (io->handle == INVALID_HANDLE_VALUE) {
        error_msg("Failed to open driver device %s: %lu", io->device_name, GetLastError());
        io->handle = NULL;
        return 0;
    }

    return 1;
}

static void close_driver(IoDriver *io) {
    if (io->handle && io->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(io->handle);
    }
    memset(io, 0, sizeof(*io));
}

static int write8(IoDriver *io, unsigned short port, unsigned char value) {
    BYTE buffer[3];
    unsigned short *port_ptr = (unsigned short *)&buffer[0];
    DWORD bytes_returned = 0;

    *port_ptr = port;
    buffer[2] = value;

    return DeviceIoControl(
        io->handle,
        IOCTL_WRITE_PORT_UCHAR,
        buffer,
        sizeof(buffer),
        NULL,
        0,
        &bytes_returned,
        NULL) ? 1 : 0;
}

static int write_reg(IoDriver *io, unsigned short reg, unsigned char value) {
    unsigned char reg_high = (unsigned char)((reg >> 8) & 0xFF);
    unsigned char reg_low = (unsigned char)(reg & 0xFF);

    return
        write8(io, INDEX_PORT, 0x2E) &&
        write8(io, DATA_PORT, 0x11) &&
        write8(io, INDEX_PORT, 0x2F) &&
        write8(io, DATA_PORT, reg_high) &&
        write8(io, INDEX_PORT, 0x2E) &&
        write8(io, DATA_PORT, 0x10) &&
        write8(io, INDEX_PORT, 0x2F) &&
        write8(io, DATA_PORT, reg_low) &&
        write8(io, INDEX_PORT, 0x2E) &&
        write8(io, DATA_PORT, 0x12) &&
        write8(io, INDEX_PORT, 0x2F) &&
        write8(io, DATA_PORT, value);
}

static int apply_mode(IoDriver *io, Mode mode, DWORD delay_ms) {
    if (!write_reg(io, REG_PARAM2, 0x00)) {
        return 0;
    }

    switch (mode) {
        case MODE_STATIC:
            if (!write_reg(io, REG_MODE, 0x02)) return 0;
            Sleep(delay_ms);
            return write_reg(io, REG_MODE, 0x03);

        case MODE_BUTTON:
            if (!write_reg(io, REG_MODE, 0x07)) return 0;
            Sleep(delay_ms);
            return write_reg(io, REG_MODE, 0x03);

        case MODE_RAINBOW:
            return write_reg(io, REG_PARAM1, 0x32) &&
                   write_reg(io, REG_PARAM2, 0x07) &&
                   write_reg(io, REG_MODE, 0x05);

        case MODE_OFF:
            return write_reg(io, REG_MODE, 0x07);

        case MODE_DEFAULT:
            return write_reg(io, REG_MODE, 0x01);

        case MODE_BREATH:
            return write_reg(io, REG_MODE, 0x02);

        case MODE_CYCLE:
            return write_reg(io, REG_MODE, 0x06);

        default:
            return 0;
    }
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

int main(int argc, char **argv) {
    const char *default_driver_path;
    const char *driver_path;
    DWORD delay_ms = 10;
    Mode mode;
    IoDriver io;
    int i;

    default_driver_path = is_x64_os() ? DEFAULT_X64_SYS : DEFAULT_X86_SYS;
    driver_path = default_driver_path;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("amr5_led_win %s\n", APP_VERSION);
        return 0;
    }

    mode = parse_mode(argv[1]);
    if (mode == MODE_INVALID) {
        usage(argv[0]);
        return 1;
    }

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("amr5_led_win %s\n", APP_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--driver") == 0) {
            if (i + 1 >= argc) {
                error_msg("%s", "--driver requires a path");
                return 1;
            }
            driver_path = argv[++i];
        } else if (strcmp(argv[i], "--delay") == 0) {
            char *end = NULL;
            unsigned long value;
            if (i + 1 >= argc) {
                error_msg("%s", "--delay requires milliseconds");
                return 1;
            }
            value = strtoul(argv[++i], &end, 10);
            if (!end || *end != '\0') {
                error_msg("Invalid delay: %s", argv[i]);
                return 1;
            }
            delay_ms = (DWORD)value;
        } else {
            error_msg("Unknown argument: %s", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!open_driver(&io, driver_path)) {
        return 2;
    }

    if (!apply_mode(&io, mode, delay_ms)) {
        error_msg("Failed to write LED registers through %s", io.driver_path);
        close_driver(&io);
        return 3;
    }

    close_driver(&io);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    attach_console_if_present();
    return main(__argc, __argv);
}
