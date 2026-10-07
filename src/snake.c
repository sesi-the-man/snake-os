/*
 * Snake: a tiny standalone snake game for a Raspberry Pi Zero with a small
 * ST7789 SPI LCD and a 5-way joystick + 3 buttons.
 *
 * Supports two screens, selectable from the main menu:
 *   240x240  1.3" ST7789 (square)
 *   320x240  ST7789 (landscape)
 * The choice is saved to the boot SD card (if present) so it sticks.
 *
 * No dependencies beyond libc and the kernel's spidev + /dev/mem.
 *
 * Controls
 *   Menu:     joystick up/down to choose, press (or KEY1) to select,
 *             left/right to change a setting, KEY3 = switch screen type
 *   Playing:  joystick steers, KEY2 pauses
 *   Paused:   KEY2 resumes, KEY3 quits to menu
 *   Game over: press (or KEY1) plays again, KEY3 goes to menu
 *
 * Build for the host with -DSIM to run a headless self-test that plays a game
 * with a simple AI and writes frames out as PPM images.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef SIM
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#endif

#define MAXW 320
#define MAXH 240

/* Screen types */
enum { DISP_240, DISP_320, NDISP };
static const char *DISP_NAME[NDISP] = {"240X240", "320X240"};
static int W = 240, H = 240;

/* ------------------------------------------------------------ framebuffer */

static uint8_t fb[MAXW * MAXH * 2]; /* RGB565, big-endian, row stride = W */

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void fill_rect(int x0, int y0, int x1, int y1, uint16_t c)
{
    /* inclusive-exclusive: [x0,x1) x [y0,y1) */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            fb[(y * W + x) * 2] = c >> 8;
            fb[(y * W + x) * 2 + 1] = c & 0xff;
        }
}

static void frame_rect(int x0, int y0, int x1, int y1, int t, uint16_t c)
{
    fill_rect(x0, y0, x1, y0 + t, c);
    fill_rect(x0, y1 - t, x1, y1, c);
    fill_rect(x0, y0, x0 + t, y1, c);
    fill_rect(x1 - t, y0, x1, y1, c);
}

static void fill_circle(int cx, int cy, int r, uint16_t c)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                fill_rect(cx + x, cy + y, cx + x + 1, cy + y + 1, c);
}

/* 5x7 font, one byte per row (low 5 bits, MSB = leftmost column).
 * Covers space, 0-9, A-Z and a little punctuation. Lowercase is upcased. */
static const uint8_t *glyph(char ch)
{
    static const uint8_t sp[7] = {0};
    static const uint8_t colon[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    static const uint8_t dash[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t excl[7] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
    static const uint8_t digits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, /* 0 */
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
        {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
        {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
    };
    static const uint8_t letters[26][7] = {
        {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
        {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
        {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, /* D */
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
        {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
        {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
        {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
        {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
        {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, /* N */
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
        {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
        {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* W */
        {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
        {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, /* Y */
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
    };
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch == ':') return colon;
    if (ch == '-') return dash;
    if (ch == '!') return excl;
    return sp;
}

/* Each glyph cell is 6 columns wide (5 + 1 spacing), scaled by s. */
static int text_width(const char *t, int s)
{
    int n = (int)strlen(t);
    return n ? n * 6 * s - s : 0;
}

static void draw_text(int x, int y, const char *t, int s, uint16_t c)
{
    for (; *t; t++, x += 6 * s) {
        const uint8_t *g = glyph(*t);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col))
                    fill_rect(x + col * s, y + row * s, x + (col + 1) * s, y + (row + 1) * s, c);
    }
}

static void text_center(int y, const char *t, int s, uint16_t c)
{
    draw_text((W - text_width(t, s)) / 2, y, t, s, c);
}

/* ------------------------------------------------------------------ input */

enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_PRESS, B_KEY1, B_KEY2, B_KEY3, B_COUNT };
#define BIT(b) (1u << (b))

#ifndef SIM
/* ---------------------------------------------------------------- hardware
 * BCM GPIO numbers for the LCD HAT wiring (both screen types use the same pins). */
static const int BTN_GPIO[B_COUNT] = {6, 19, 5, 26, 13, 21, 20, 16};
#define LCD_DC 25
#define LCD_RST 27
#define LCD_BL 24

static volatile uint32_t *gpio;
static int spi_fd = -1;

static void die(const char *what)
{
    fprintf(stderr, "snake: %s: %s\n", what, strerror(errno));
    exit(1);
}

/* Device nodes may take a moment to appear right after boot, so retry. */
static int open_retry(const char *path, int flags)
{
    for (int i = 0; i < 50; i++) {
        int fd = open(path, flags);
        if (fd >= 0) return fd;
        usleep(100000);
    }
    return -1;
}

static uint32_t peripheral_base(void)
{
    /* /proc/device-tree/soc/ranges: <child-addr> <parent-addr> <size>, big-endian */
    uint8_t buf[8];
    uint32_t base = 0x20000000; /* BCM2835 (Pi Zero / Pi 1) default */
    int fd = open("/proc/device-tree/soc/ranges", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, buf, 8) == 8) {
            uint32_t v = (uint32_t)buf[4] << 24 | buf[5] << 16 | buf[6] << 8 | buf[7];
            if (v) base = v;
        }
        close(fd);
    }
    return base;
}

static void gpio_init(void)
{
    int fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    off_t off = 0;
    if (fd < 0) {
        fd = open_retry("/dev/mem", O_RDWR | O_SYNC);
        off = peripheral_base() + 0x200000;
    }
    if (fd < 0) die("open /dev/mem");
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, off);
    if (p == MAP_FAILED) die("mmap gpio");
    close(fd);
    gpio = p;
}

static void gpio_mode(int pin, int out)
{
    volatile uint32_t *sel = gpio + pin / 10;
    int sh = (pin % 10) * 3;
    *sel = (*sel & ~(7u << sh)) | ((out ? 1u : 0u) << sh);
}

static void gpio_write(int pin, int v)
{
    gpio[v ? 7 : 10] = 1u << pin; /* GPSET0 / GPCLR0 */
}

static int gpio_read(int pin)
{
    return (gpio[13] >> pin) & 1; /* GPLEV0 */
}

static void gpio_pullups(uint32_t mask)
{
    /* BCM2835 pull sequence: GPPUD then clock it into the selected pins */
    gpio[37] = 2; /* GPPUD = pull-up */
    usleep(20);
    gpio[38] = mask; /* GPPUDCLK0 */
    usleep(20);
    gpio[37] = 0;
    gpio[38] = 0;
}

static void spi_write(const uint8_t *buf, size_t len)
{
    while (len) {
        size_t n = len > 4096 ? 4096 : len; /* spidev's default buffer size */
        if (write(spi_fd, buf, n) != (ssize_t)n) die("spi write");
        buf += n;
        len -= n;
    }
}

static void lcd_cmd(uint8_t c, const uint8_t *data, size_t n)
{
    gpio_write(LCD_DC, 0);
    spi_write(&c, 1);
    if (n) {
        gpio_write(LCD_DC, 1);
        spi_write(data, n);
    }
}
#define CMD(c, ...)                                               \
    do {                                                          \
        static const uint8_t d_[] = {__VA_ARGS__};                \
        lcd_cmd(c, d_, sizeof d_);                                \
    } while (0)
#define CMD0(c) lcd_cmd(c, NULL, 0)

static void lcd_hw_init(void)
{
    spi_fd = open_retry("/dev/spidev0.0", O_RDWR);
    if (spi_fd < 0) die("open /dev/spidev0.0");
    uint8_t mode = SPI_MODE_0, bits = 8;
    uint32_t speed = 40000000;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) die("spi speed");
    gpio_mode(LCD_DC, 1);
    gpio_mode(LCD_RST, 1);
    gpio_mode(LCD_BL, 1);
}

/* 240x240 square panel (init sequence of the common Waveshare 1.3" driver) */
static void lcd_init_240(void)
{
    gpio_write(LCD_BL, 1);
    gpio_write(LCD_RST, 1); usleep(10000);
    gpio_write(LCD_RST, 0); usleep(10000);
    gpio_write(LCD_RST, 1); usleep(10000);
    CMD(0x36, 0x70);
    CMD(0x3A, 0x05);
    CMD(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33);
    CMD(0xB7, 0x35);
    CMD(0xBB, 0x19);
    CMD(0xC0, 0x2C);
    CMD(0xC2, 0x01);
    CMD(0xC3, 0x12);
    CMD(0xC4, 0x20);
    CMD(0xC6, 0x0F);
    CMD(0xD0, 0xA4, 0xA1);
    CMD(0xE0, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23);
    CMD(0xE1, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23);
    CMD0(0x21); /* inversion on */
    CMD0(0x11); /* sleep out */
    usleep(120000);
    CMD0(0x29); /* display on */
}

/* 320x240 panel: natively 240x320 portrait, driven in landscape.
 * Init sequence of the common st7789 "mpy" driver, sent twice as that driver does. */
static void lcd_init_320(void)
{
    gpio_write(LCD_RST, 1); usleep(10000);
    gpio_write(LCD_RST, 0); usleep(10000);
    gpio_write(LCD_RST, 1); usleep(120000);
    for (int i = 0; i < 2; i++) {
        CMD(0x11, 0x00); usleep(120000); /* sleep out */
        CMD(0x13, 0x00);                 /* normal display mode */
        CMD(0xB6, 0x0A, 0x82);
        CMD(0x3A, 0x55); usleep(10000);  /* 16-bit color */
        CMD(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33);
        CMD(0xB7, 0x35);
        CMD(0xBB, 0x28);
        CMD(0xC0, 0x0C);
        CMD(0xC2, 0x01, 0xFF);
        CMD(0xC3, 0x10);
        CMD(0xC4, 0x20);
        CMD(0xC6, 0x0F);
        CMD(0xD0, 0xA4, 0xA1);
        CMD(0xE0, 0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17);
        CMD(0xE1, 0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E);
        CMD(0x21, 0x00);                 /* inversion on */
        CMD(0x29, 0x00); usleep(120000); /* display on */
    }
    CMD(0x36, 0x68); /* landscape (MV|MX) + BGR */
    gpio_write(LCD_BL, 1);
}

static void lcd_init(int disp)
{
    if (disp == DISP_320) lcd_init_320();
    else lcd_init_240();
}

static void lcd_show(void)
{
    uint8_t ca[4] = {0, 0, (uint8_t)((W - 1) >> 8), (uint8_t)((W - 1) & 0xff)};
    uint8_t ra[4] = {0, 0, (uint8_t)((H - 1) >> 8), (uint8_t)((H - 1) & 0xff)};
    lcd_cmd(0x2A, ca, 4);
    lcd_cmd(0x2B, ra, 4);
    CMD0(0x2C);
    gpio_write(LCD_DC, 1);
    spi_write(fb, (size_t)W * H * 2);
}

static void buttons_init(void)
{
    uint32_t mask = 0;
    for (int i = 0; i < B_COUNT; i++) {
        gpio_mode(BTN_GPIO[i], 0);
        mask |= 1u << BTN_GPIO[i];
    }
    gpio_pullups(mask);
}

/* Bitmask of buttons that went down since the last call (active-low). */
static unsigned buttons_pressed(void)
{
    static unsigned prev;
    unsigned now = 0;
    for (int i = 0; i < B_COUNT; i++)
        if (!gpio_read(BTN_GPIO[i])) now |= 1u << i;
    unsigned edges = now & ~prev;
    prev = now;
    return edges;
}

/* ----------------------------------------------------------- saved settings
 * A two-line text file on the boot SD card's FAT partition. Best effort: if
 * the card isn't there, settings just last until power-off. */
#define MNT "/tmp/sd"
#define CFG MNT "/game.cfg"

static int sd_mount(int rw)
{
    static const char *devs[] = {"/dev/mmcblk0p1", "/dev/mmcblk0"};
    unsigned long fl = rw ? MS_SYNCHRONOUS : MS_RDONLY;
    mkdir(MNT, 0700);
    for (int i = 0; i < 2; i++) {
        /* "nodirty" keeps FAT's dirty flag off (kernel patch); fall back without it */
        if (mount(devs[i], MNT, "vfat", fl, rw ? "nodirty" : NULL) == 0) return 1;
        if (rw && mount(devs[i], MNT, "vfat", fl, NULL) == 0) return 1;
    }
    return 0;
}

static void settings_load(int *disp, int *speed)
{
    if (!sd_mount(0)) return;
    FILE *f = fopen(CFG, "r");
    if (f) {
        int d, s;
        if (fscanf(f, "display=%d speed=%d", &d, &s) == 2) {
            if (d >= 0 && d < NDISP) *disp = d;
            if (s >= 0 && s < 3) *speed = s;
        }
        fclose(f);
    }
    umount(MNT);
}

static void settings_save(int disp, int speed)
{
    if (!sd_mount(1)) return;
    FILE *f = fopen(CFG, "w");
    if (f) {
        fprintf(f, "display=%d\nspeed=%d\n", disp, speed);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
    sync();
    umount(MNT);
}
#endif /* !SIM */

/* ------------------------------------------------------------------- game */

#define CELL 12
#define HUD 24
#define ROWS ((MAXH - HUD) / CELL) /* 18 */
#define MAXCOLS (MAXW / CELL)      /* 26 */
#define MAXLEN (MAXCOLS * ROWS)

static int COLS = 20; /* W / CELL */
static int OX = 0;    /* horizontal offset to center the board */

enum { S_MENU, S_PLAYING, S_PAUSED, S_OVER };
enum { M_PLAY, M_SPEED, M_DISPLAY, M_COUNT };

static const struct { const char *name; int ms; } SPEEDS[] = {
    {"SLOW", 180}, {"NORMAL", 120}, {"FAST", 80}};
#define NSPEEDS 3

static struct {
    int state, speed, disp, menu_sel, score, best;
    int len;
    int8_t bx[MAXLEN], by[MAXLEN]; /* body, head first */
    int dx, dy;
    int qx[3], qy[3], qn;          /* buffered turns so fast inputs aren't lost */
    int fx, fy;                    /* food; fx < 0 means none */
    int disp_changed, settings_changed;
} g;

static uint16_t C_BG, C_GRID, C_HUD, C_SNAKE, C_HEAD, C_FOOD, C_TEXT, C_DIM, C_BOX, C_SEL;

static void colors_init(void)
{
    C_BG = rgb(8, 10, 12);
    C_GRID = rgb(18, 22, 26);
    C_HUD = rgb(20, 24, 28);
    C_SNAKE = rgb(70, 200, 90);
    C_HEAD = rgb(160, 240, 150);
    C_FOOD = rgb(230, 60, 60);
    C_TEXT = rgb(235, 235, 235);
    C_DIM = rgb(130, 130, 130);
    C_BOX = rgb(0, 0, 0);
    C_SEL = rgb(70, 200, 90);
}

static void set_display(int d)
{
    g.disp = d;
    W = d == DISP_320 ? 320 : 240;
    H = 240;
    COLS = W / CELL;
    OX = (W - COLS * CELL) / 2;
}

static int on_body(int x, int y, int n)
{
    for (int i = 0; i < n; i++)
        if (g.bx[i] == x && g.by[i] == y) return 1;
    return 0;
}

static void place_food(void)
{
    int free = COLS * ROWS - g.len;
    if (free <= 0) { g.fx = -1; return; }
    int k = rand() % free;
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if (!on_body(x, y, g.len) && k-- == 0) { g.fx = x; g.fy = y; return; }
}

static void game_reset(void)
{
    int cx = COLS / 2, cy = ROWS / 2;
    g.len = 3;
    for (int i = 0; i < 3; i++) { g.bx[i] = cx - i; g.by[i] = cy; }
    g.dx = 1; g.dy = 0; g.qn = 0; g.score = 0;
    place_food();
}

static void game_over(void)
{
    g.state = S_OVER;
    if (g.score > g.best) g.best = g.score;
}

static void turn(int dx, int dy)
{
    int lx = g.qn ? g.qx[g.qn - 1] : g.dx;
    int ly = g.qn ? g.qy[g.qn - 1] : g.dy;
    if ((dx == lx && dy == ly) || (dx == -lx && dy == -ly) || g.qn >= 3) return;
    g.qx[g.qn] = dx; g.qy[g.qn] = dy; g.qn++;
}

static void step(void)
{
    if (g.qn) {
        g.dx = g.qx[0]; g.dy = g.qy[0];
        memmove(g.qx, g.qx + 1, sizeof(int) * 2);
        memmove(g.qy, g.qy + 1, sizeof(int) * 2);
        g.qn--;
    }
    int nx = g.bx[0] + g.dx, ny = g.by[0] + g.dy;
    int eating = (nx == g.fx && ny == g.fy);
    /* the tail moves out of the way this tick unless we're growing */
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS || on_body(nx, ny, eating ? g.len : g.len - 1)) {
        game_over();
        return;
    }
    if (eating) g.len++;
    if (g.len < 2 || g.len > MAXLEN) { game_over(); return; } /* can't happen */
    memmove(g.bx + 1, g.bx, (size_t)(g.len - 1));
    memmove(g.by + 1, g.by, (size_t)(g.len - 1));
    g.bx[0] = nx; g.by[0] = ny;
    if (eating) {
        g.score++;
        place_food();
        if (g.fx < 0) game_over(); /* filled the board */
    }
}

static void toggle_display(void)
{
    set_display((g.disp + 1) % NDISP);
    g.disp_changed = 1;
    g.settings_changed = 1;
}

static void handle(unsigned p)
{
    int start = p & (BIT(B_PRESS) | BIT(B_KEY1));
    switch (g.state) {
    case S_MENU:
        if (p & BIT(B_KEY3)) { toggle_display(); break; }
        if (p & BIT(B_UP)) g.menu_sel = (g.menu_sel + M_COUNT - 1) % M_COUNT;
        if (p & BIT(B_DOWN)) g.menu_sel = (g.menu_sel + 1) % M_COUNT;
        if (start || (p & (BIT(B_LEFT) | BIT(B_RIGHT)))) {
            int back = (p & BIT(B_LEFT)) != 0;
            if (g.menu_sel == M_PLAY) {
                if (start) { game_reset(); g.state = S_PLAYING; }
            } else if (g.menu_sel == M_SPEED) {
                g.speed = (g.speed + (back ? NSPEEDS - 1 : 1)) % NSPEEDS;
                g.settings_changed = 1;
            } else if (g.menu_sel == M_DISPLAY) {
                toggle_display();
            }
        }
        break;
    case S_PLAYING:
        if (p & BIT(B_UP)) turn(0, -1);
        if (p & BIT(B_DOWN)) turn(0, 1);
        if (p & BIT(B_LEFT)) turn(-1, 0);
        if (p & BIT(B_RIGHT)) turn(1, 0);
        if (p & BIT(B_KEY2)) g.state = S_PAUSED;
        break;
    case S_PAUSED:
        if ((p & BIT(B_KEY2)) || start) g.state = S_PLAYING;
        else if (p & BIT(B_KEY3)) g.state = S_MENU;
        break;
    case S_OVER:
        if (start) { game_reset(); g.state = S_PLAYING; }
        else if (p & (BIT(B_KEY3) | BIT(B_KEY2))) g.state = S_MENU;
        break;
    }
}

static void render_menu(void)
{
    char buf[32];
    text_center(28, "SNAKE", 6, C_SNAKE);
    const char *items[M_COUNT];
    char sp[24], dp[24];
    snprintf(sp, sizeof sp, "SPEED: %s", SPEEDS[g.speed].name);
    snprintf(dp, sizeof dp, "SCREEN: %s", DISP_NAME[g.disp]);
    items[M_PLAY] = "PLAY";
    items[M_SPEED] = sp;
    items[M_DISPLAY] = dp;
    for (int i = 0; i < M_COUNT; i++) {
        int y = 100 + i * 32;
        int w = text_width(items[i], 2);
        int x = (W - w) / 2;
        if (i == g.menu_sel) {
            fill_rect(x - 10, y - 6, x + w + 10, y + 20, C_SEL);
            draw_text(x, y, items[i], 2, C_BOX);
        } else {
            draw_text(x, y, items[i], 2, C_TEXT);
        }
    }
    if (g.best) {
        snprintf(buf, sizeof buf, "BEST: %d", g.best);
        text_center(200, buf, 2, C_DIM);
    }
    text_center(224, "KEY3: SWITCH SCREEN", 1, C_DIM);
}

static void render(void)
{
    char buf[32];
    fill_rect(0, 0, W, H, C_BG);

    if (g.state == S_MENU) {
        render_menu();
        return;
    }

    /* HUD */
    fill_rect(0, 0, W, HUD, C_HUD);
    snprintf(buf, sizeof buf, "SCORE %d", g.score);
    draw_text(8, 5, buf, 2, C_TEXT);
    snprintf(buf, sizeof buf, "BEST %d", g.score > g.best ? g.score : g.best);
    draw_text(W - 8 - text_width(buf, 2), 5, buf, 2, C_DIM);

    /* board */
    for (int x = 0; x <= COLS; x++) fill_rect(OX + x * CELL, HUD, OX + x * CELL + 1, H, C_GRID);
    for (int y = HUD; y < H; y += CELL) fill_rect(OX, y, OX + COLS * CELL + 1, y + 1, C_GRID);
    if (g.fx >= 0)
        fill_circle(OX + g.fx * CELL + CELL / 2, HUD + g.fy * CELL + CELL / 2, CELL / 2 - 2, C_FOOD);
    for (int i = g.len - 1; i >= 0; i--)
        fill_rect(OX + g.bx[i] * CELL + 1, HUD + g.by[i] * CELL + 1,
                  OX + g.bx[i] * CELL + CELL, HUD + g.by[i] * CELL + CELL, i ? C_SNAKE : C_HEAD);

    if (g.state == S_PAUSED || g.state == S_OVER) {
        int bx0 = W / 2 - 100, bx1 = W / 2 + 100;
        fill_rect(bx0, 80, bx1, 174, C_BOX);
        frame_rect(bx0, 80, bx1, 174, 2, C_SNAKE);
        if (g.state == S_PAUSED) {
            text_center(94, "PAUSED", 4, C_TEXT);
            text_center(134, "KEY2 TO RESUME", 2, C_DIM);
            text_center(156, "KEY3: MENU", 1, C_DIM);
        } else {
            text_center(90, "GAME OVER", 3, C_SNAKE);
            text_center(122, "PRESS TO RETRY", 2, C_TEXT);
            text_center(150, "KEY3: MENU", 2, C_DIM);
        }
    }
}

#ifdef SIM
/* ---------------------------------------------------------- host self-test */
static void dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t v = fb[i * 2] << 8 | fb[i * 2 + 1];
        fputc((v >> 11) << 3, f);
        fputc(((v >> 5) & 63) << 2, f);
        fputc((v & 31) << 3, f);
    }
    fclose(f);
}

static int play_ai(int max_steps, const char *snap)
{
    int steps = 0;
    while (g.state == S_PLAYING && steps < max_steps) {
        if (snap && steps == 150) { render(); dump(snap); }
        static const int D[4][3] = {{0, -1, B_UP}, {0, 1, B_DOWN}, {-1, 0, B_LEFT}, {1, 0, B_RIGHT}};
        int best = -1, bd = 1 << 30;
        for (int i = 0; i < 4; i++) {
            int nx = g.bx[0] + D[i][0], ny = g.by[0] + D[i][1];
            if (D[i][0] == -g.dx && D[i][1] == -g.dy) continue;
            if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS || on_body(nx, ny, g.len - 1)) continue;
            int d = abs(nx - g.fx) + abs(ny - g.fy);
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) handle(BIT(D[best][2]));
        step();
        render();
        steps++;
    }
    return steps;
}

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    colors_init();
    srand(1);
    g.speed = 1;
    set_display(DISP_240);

    render(); dump("m240.ppm");
    /* menu navigation: down to SPEED, right cycles, down to SCREEN, press toggles */
    handle(BIT(B_DOWN)); CHECK(g.menu_sel == M_SPEED);
    handle(BIT(B_RIGHT)); CHECK(g.speed == 2 && g.settings_changed);
    handle(BIT(B_LEFT)); CHECK(g.speed == 1);
    handle(BIT(B_DOWN)); CHECK(g.menu_sel == M_DISPLAY);
    handle(BIT(B_PRESS)); CHECK(g.disp == DISP_320 && W == 320 && COLS == 26 && g.disp_changed);
    render(); dump("m320.ppm");
    handle(BIT(B_KEY3)); CHECK(g.disp == DISP_240 && W == 240);   /* KEY3 shortcut */
    handle(BIT(B_KEY3)); CHECK(g.disp == DISP_320);
    handle(BIT(B_DOWN)); CHECK(g.menu_sel == M_PLAY);               /* wraps */
    handle(BIT(B_PRESS)); CHECK(g.state == S_PLAYING);

    int s1 = play_ai(1000, "p320.ppm");
    while (g.state == S_PLAYING) step();
    render(); dump("o320.ppm");
    CHECK(g.state == S_OVER);
    int sc320 = g.score;
    handle(BIT(B_KEY3)); CHECK(g.state == S_MENU);

    handle(BIT(B_KEY3)); CHECK(g.disp == DISP_240);
    handle(BIT(B_KEY1)); CHECK(g.state == S_PLAYING);
    int s2 = play_ai(1000, "p240.ppm");
    while (g.state == S_PLAYING) step();
    CHECK(g.state == S_OVER);
    g.state = S_PAUSED; render(); dump("pause240.ppm"); g.state = S_OVER;
    handle(BIT(B_PRESS)); CHECK(g.state == S_PLAYING && g.score == 0);
    handle(BIT(B_KEY2)); CHECK(g.state == S_PAUSED);
    handle(BIT(B_KEY3)); CHECK(g.state == S_MENU);
    /* reversal rejected, two queued turns kept */
    game_reset(); g.state = S_PLAYING;
    handle(BIT(B_LEFT)); CHECK(g.qn == 0);
    handle(BIT(B_UP)); handle(BIT(B_LEFT)); CHECK(g.qn == 2);
    printf("320: steps=%d score=%d | 240: steps=%d best=%d\nok\n", s1, sc320, s2, g.best);
    return 0;
}
#else
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main(void)
{
    colors_init();
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    g.speed = 1;
    int disp = DISP_240;
    settings_load(&disp, &g.speed);
    set_display(disp);

    gpio_init();
    lcd_hw_init();
    lcd_init(g.disp);
    buttons_init();

    render();
    lcd_show();
    long next = now_ms();
    for (;;) {
        int dirty = 0;
        unsigned p = buttons_pressed();
        if (p) {
            handle(p);
            if (g.disp_changed) {
                lcd_init(g.disp); /* re-initialize the panel for the new screen type */
                g.disp_changed = 0;
            }
            dirty = 1;
            next = now_ms() + SPEEDS[g.speed].ms;
        }
        long t = now_ms();
        if (g.state == S_PLAYING && t >= next) {
            step();
            next = t + SPEEDS[g.speed].ms;
            dirty = 1;
        }
        if (dirty) {
            render();
            lcd_show();
        }
        if (g.settings_changed && g.state == S_MENU) {
            settings_save(g.disp, g.speed);
            g.settings_changed = 0;
        }
        usleep(5000); /* ~200 Hz input polling */
    }
}
#endif
