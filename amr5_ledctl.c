/*
 * amr5_ledctl.c - Acemagic AMR5 LED mode control
 *
 * Controls LED lighting modes on the Acemagic AMR5 mini PC (Ryzen 5 5600U)
 * via direct I/O port writes to the onboard Super I/O chip.
 *
 * Supported platforms:
 *   Linux  - requires root; uses ioperm() + outb()
 *   Windows - requires inpoutx64 driver; uses DeviceIoControl()
 *
 * Protocol overview:
 *   The LED controller is accessed indirectly via a Super I/O chip at I/O
 *   ports 0x4E (index) and 0x4F (data).  A double-indirect scheme is used:
 *
 *     Outer ports:    0x4E = Super I/O index,  0x4F = Super I/O data
 *     Inner registers: 0x2E = inner index,      0x2F = inner data
 *
 *   To write a value to inner register R:
 *     outb(IDX_PORT, 0x2E)   -- select inner index register
 *     outb(DAT_PORT, R)      -- set inner register address to R
 *     outb(IDX_PORT, 0x2F)   -- select inner data register
 *     outb(DAT_PORT, V)      -- write value V to register R
 *
 *   Each LED zone requires three inner-register writes per update:
 *     inner reg 0x11 = bank/register selector (always 0x04)
 *     inner reg 0x10 = zone device address (hardware-specific)
 *     inner reg 0x12 = mode command value  (mode-specific)
 *
 *   The full mode sequence is sent NUM_PASSES times with a short delay
 *   between passes to ensure reliable delivery.
 *
 * Mode command bytes:
 *   The "off" mode bytes are verified via reverse engineering of AMR5LedOff
 *   (https://github.com/sergmuz/AMR5LedOff).
 *   The "breathing", "cycling", and "static" mode bytes follow the same
 *   protocol structure but the exact values require hardware verification
 *   by sniffing I/O port writes made by the official Acemagic LED software.
 *
 * References:
 *   AMR5LedOff: https://github.com/sergmuz/AMR5LedOff
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/io.h>
#endif

/* -------------------------------------------------------------------------
 * Hardware constants
 * -------------------------------------------------------------------------
 * IDX_PORT  Super I/O index port - write a register address here first
 * DAT_PORT  Super I/O data port  - then read or write the register value
 */
#define IDX_PORT   0x4E
#define DAT_PORT   0x4F

/* Inner (double-indirect) register addresses within the Super I/O */
#define INNER_IDX  0x2E   /* inner index register's address in outer space */
#define INNER_DAT  0x2F   /* inner data  register's address in outer space */

/* Inner register addresses used for LED zone control */
#define REG_BANK   0x11   /* bank/register selector (always BANK_VAL) */
#define REG_ADDR   0x10   /* zone device address */
#define REG_CMD    0x12   /* mode command value */
#define BANK_VAL   0x04   /* constant bank value */

/* LED configuration */
#define NUM_ZONES  5      /* number of LED zones on the AMR5 */
#define NUM_PASSES 3      /* transmission passes per mode change */
#define PASS_DELAY 80     /* ms delay between passes */
#define CMD_DELAY  2      /* ms delay between zone writes */

/* -------------------------------------------------------------------------
 * Per-zone LED command
 * -------------------------------------------------------------------------
 * bank  : value written to inner reg 0x11 (selects LED register in device)
 * addr  : value written to inner reg 0x10 (zone I2C/device address)
 * value : value written to inner reg 0x12 (mode command byte)
 */
typedef struct {
    uint8_t bank;
    uint8_t addr;
    uint8_t value;
} zone_cmd_t;

/* -------------------------------------------------------------------------
 * LED mode command tables
 * -------------------------------------------------------------------------
 * Each mode has NUM_ZONES zone_cmd_t entries sent in order.
 *
 * "off" is the only verified mode.  The others share the same protocol
 * structure but their exact .value bytes need hardware validation.
 * To find correct values, monitor I/O port writes with inpoutx64 while
 * switching modes in the official Acemagic LED software on Windows, then
 * update the tables below.
 */

/* off: all LEDs dark - verified from AMR5LedOff reverse engineering */
static const zone_cmd_t MODE_OFF[NUM_ZONES] = {
    {0x04, 0xBD, 0x01},
    {0x04, 0xBF, 0x00},
    {0x04, 0x5C, 0x00},
    {0x04, 0xBE, 0x07},
    {0x04, 0xBE, 0x03},
};

/*
 * breathing: LEDs slowly pulse on and off.
 * NOTE: values require hardware verification.
 */
static const zone_cmd_t MODE_BREATHING[NUM_ZONES] = {
    {0x04, 0xBD, 0x02},
    {0x04, 0xBF, 0x02},
    {0x04, 0x5C, 0x02},
    {0x04, 0xBE, 0x02},
    {0x04, 0xBE, 0x02},
};

/*
 * cycling: LEDs cycle through colors (rainbow effect).
 * NOTE: values require hardware verification.
 */
static const zone_cmd_t MODE_CYCLING[NUM_ZONES] = {
    {0x04, 0xBD, 0x03},
    {0x04, 0xBF, 0x03},
    {0x04, 0x5C, 0x03},
    {0x04, 0xBE, 0x03},
    {0x04, 0xBE, 0x03},
};

/*
 * static: LEDs solid on at their factory default color.
 * NOTE: values require hardware verification.
 */
static const zone_cmd_t MODE_STATIC[NUM_ZONES] = {
    {0x04, 0xBD, 0x01},
    {0x04, 0xBF, 0x04},
    {0x04, 0x5C, 0x04},
    {0x04, 0xBE, 0x05},
    {0x04, 0xBE, 0x04},
};

/* -------------------------------------------------------------------------
 * Mode registry
 * -------------------------------------------------------------------------
 */
typedef struct {
    const char       *name;
    const char       *description;
    const zone_cmd_t *cmds;
} led_mode_t;

static const led_mode_t MODES[] = {
    {"off",      "Turn all LEDs off (verified)",                        MODE_OFF},
    {"breathing","Breathing/pulsing effect (needs hw verification)",    MODE_BREATHING},
    {"cycling",  "Color cycling / rainbow effect (needs hw verification)", MODE_CYCLING},
    {"static",   "Solid-on at factory default color (needs hw verification)", MODE_STATIC},
};
#define NUM_MODES ((int)(sizeof(MODES) / sizeof(MODES[0])))

/* -------------------------------------------------------------------------
 * Platform-specific I/O port access
 * -------------------------------------------------------------------------
 */
#ifdef _WIN32

/* Windows: access I/O ports via the inpoutx64 kernel driver.
 * The driver must be installed before running this program.
 * Download: https://www.highrez.co.uk/downloads/inpout32.htm
 * or bundled with AMR5LedOff releases.
 */
#define DEVICE_PATH   "\\\\.\\inpoutx64"
#define IOCTL_WRITE   ((DWORD)0x9C402008)
#define DEVICE_RETRIES    5
#define DEVICE_RETRY_MS   600

typedef struct {
    uint16_t port;
    uint8_t  value;
} __attribute__((packed)) ioctl_write_t;

static HANDLE g_dev = INVALID_HANDLE_VALUE;

static int platform_init(void)
{
    int attempt;
    for (attempt = 1; attempt <= DEVICE_RETRIES; attempt++) {
        g_dev = CreateFileA(DEVICE_PATH, GENERIC_WRITE, 0, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_dev != INVALID_HANDLE_VALUE)
            return 0;
        fprintf(stderr, "inpoutx64: open attempt %d failed (err %lu)\n",
                attempt, (unsigned long)GetLastError());
        Sleep(DEVICE_RETRY_MS);
    }
    fprintf(stderr,
            "error: cannot open inpoutx64 device.\n"
            "       Make sure the inpoutx64 driver is installed.\n"
            "       See README.md for installation instructions.\n");
    return -1;
}

static void platform_cleanup(void)
{
    if (g_dev != INVALID_HANDLE_VALUE) {
        CloseHandle(g_dev);
        g_dev = INVALID_HANDLE_VALUE;
    }
}

static int outport(uint16_t port, uint8_t value)
{
    ioctl_write_t cmd;
    DWORD returned = 0;

    cmd.port  = port;
    cmd.value = value;
    if (!DeviceIoControl(g_dev, IOCTL_WRITE,
                         &cmd, sizeof(cmd),
                         NULL, 0, &returned, NULL)) {
        fprintf(stderr, "DeviceIoControl failed (err %lu)\n",
                (unsigned long)GetLastError());
        return -1;
    }
    return 0;
}

static void ms_sleep(unsigned int ms)
{
    Sleep(ms);
}

#else /* Linux */

static int platform_init(void)
{
    if (ioperm(IDX_PORT, 2, 1) != 0) {
        perror("ioperm");
        fprintf(stderr,
                "error: cannot obtain I/O port access.\n"
                "       Run as root or with CAP_SYS_RAWIO capability.\n");
        return -1;
    }
    return 0;
}

static void platform_cleanup(void)
{
    ioperm(IDX_PORT, 2, 0);
}

static int outport(uint16_t port, uint8_t value)
{
    outb(value, port);
    return 0;
}

static void ms_sleep(unsigned int ms)
{
    usleep((useconds_t)ms * 1000U);
}

#endif /* _WIN32 */

/* -------------------------------------------------------------------------
 * LED control
 * -------------------------------------------------------------------------
 */

/*
 * Write a single value to an inner register of the Super I/O LED controller.
 *
 * This uses the double-indirect scheme:
 *   outb(IDX_PORT, INNER_IDX)  -- select outer reg that holds inner index
 *   outb(DAT_PORT, inner_reg)  -- set the inner register address
 *   outb(IDX_PORT, INNER_DAT)  -- select outer reg that holds inner data
 *   outb(DAT_PORT, value)      -- write value into the inner register
 */
static int inner_write(uint8_t inner_reg, uint8_t value)
{
    if (outport(IDX_PORT, INNER_IDX) != 0) return -1;
    if (outport(DAT_PORT, inner_reg) != 0) return -1;
    if (outport(IDX_PORT, INNER_DAT) != 0) return -1;
    if (outport(DAT_PORT, value)     != 0) return -1;
    return 0;
}

/*
 * Apply a single zone command (bank + addr + value → inner regs).
 */
static int send_zone(const zone_cmd_t *z)
{
    if (inner_write(REG_BANK, z->bank)  != 0) return -1;
    if (inner_write(REG_ADDR, z->addr)  != 0) return -1;
    if (inner_write(REG_CMD,  z->value) != 0) return -1;
    return 0;
}

/*
 * Apply a complete LED mode command table.
 * Sends all NUM_ZONES zone commands NUM_PASSES times for reliability.
 */
static int apply_mode(const zone_cmd_t *cmds)
{
    int pass, i;

    for (pass = 0; pass < NUM_PASSES; pass++) {
        for (i = 0; i < NUM_ZONES; i++) {
            if (send_zone(&cmds[i]) != 0)
                return -1;
            ms_sleep(CMD_DELAY);
        }
        if (pass < NUM_PASSES - 1)
            ms_sleep(PASS_DELAY);
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Entry point
 * -------------------------------------------------------------------------
 */
static void print_usage(const char *prog)
{
    int i;
    fprintf(stderr,
            "Usage: %s <mode>\n"
            "       %s --raw <z1> <z2> <z3> <z4> <z5>\n"
            "\n"
            "Modes:\n",
            prog, prog);
    for (i = 0; i < NUM_MODES; i++)
        fprintf(stderr, "  %-10s %s\n", MODES[i].name, MODES[i].description);
    fprintf(stderr,
            "\n"
            "Raw mode:\n"
            "  Specify a hex mode-value byte for each of the %d zones.\n"
            "  Zone addresses are fixed; only the command byte (reg 0x12) varies.\n"
            "  Example (equivalent to 'off'): %s --raw 01 00 00 07 03\n",
            NUM_ZONES, prog);
}

int main(int argc, char *argv[])
{
    const char *prog = argv[0];
    int i;

    if (argc < 2) {
        print_usage(prog);
        return EXIT_FAILURE;
    }

    /* --raw <z1> <z2> <z3> <z4> <z5> */
    if (strcmp(argv[1], "--raw") == 0) {
        zone_cmd_t raw[NUM_ZONES];
        unsigned long v;
        char *end;

        if (argc != 2 + NUM_ZONES) {
            fprintf(stderr, "error: --raw requires exactly %d hex values\n",
                    NUM_ZONES);
            print_usage(prog);
            return EXIT_FAILURE;
        }
        for (i = 0; i < NUM_ZONES; i++) {
            v = strtoul(argv[2 + i], &end, 16);
            if (*end != '\0' || v > 0xFF) {
                fprintf(stderr, "error: invalid hex byte '%s'\n", argv[2 + i]);
                return EXIT_FAILURE;
            }
            raw[i].bank  = BANK_VAL;
            raw[i].addr  = MODE_OFF[i].addr; /* zone addresses are fixed */
            raw[i].value = (uint8_t)v;
        }

        if (platform_init() != 0)
            return EXIT_FAILURE;
        i = apply_mode(raw);
        platform_cleanup();
        if (i != 0) {
            fprintf(stderr, "error: failed to apply raw mode\n");
            return EXIT_FAILURE;
        }
        printf("Raw LED mode applied.\n");
        return EXIT_SUCCESS;
    }

    /* Named mode */
    for (i = 0; i < NUM_MODES; i++) {
        if (strcmp(argv[1], MODES[i].name) == 0) {
            if (platform_init() != 0)
                return EXIT_FAILURE;
            if (apply_mode(MODES[i].cmds) != 0) {
                platform_cleanup();
                fprintf(stderr, "error: failed to apply mode '%s'\n",
                        MODES[i].name);
                return EXIT_FAILURE;
            }
            platform_cleanup();
            printf("LED mode '%s' applied.\n", MODES[i].name);
            return EXIT_SUCCESS;
        }
    }

    fprintf(stderr, "error: unknown mode '%s'\n", argv[1]);
    print_usage(prog);
    return EXIT_FAILURE;
}
