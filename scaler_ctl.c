/*
 * Interactive SIS3820 scaler control via Wiener VM-USB / libxxusb.
 *
 * Build (from this directory):
 *   gcc -O -Wall -fPIC -g -I../include scaler_ctl.c \
 *       -lxx_usb -lm -lusb -L../lib -Wl,-rpath="$(pwd)/../lib" \
 *       -o scaler_ctl
 *
 * Commands: r=reset  g=start  s=stop  m=monitor  a=auto [sec]
 *           cga=clear+go+auto  p=period [sec]  c=clear  h=help  q=quit
 */

#include <libxxusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/select.h>
#include <unistd.h>

#define AUTO_PERIOD_DEFAULT  1
#define AUTO_PERIOD_MIN      1
#define AUTO_PERIOD_MAX      3600

#define BASE          0x88000000UL
#define AM            0x09        /* A32 user data */

#define REG_ID        0x04
#define REG_ACQMODE   0x100
#define REG_INHIBIT   0x200       /* per-channel count inhibit */
#define KEY_RESET     0x400
#define KEY_FIFO_RST  0x404
#define KEY_CLEAR     0x40C
#define KEY_LNE       0x410
#define KEY_ARM       0x414
#define KEY_ENABLE    0x418
#define KEY_DISABLE   0x41C
#define SHADOW_BASE   0x800

/* Acq mode: 32-bit, VME LNE, SRAM, input mode NONE (control pins unused),
 * 50 MHz out, latching scaler, NON-clearing (bit0=1).
 * Do NOT use input mode 4 (0x00040000): that makes control inputs inhibit
 * channel banks — accidental cables on CTRL look like “no counts”. */
#define ACQ_MODE_DEFAULT \
  (0x00000001UL | 0x00001000UL | 0x00100000UL)

static usb_dev_handle *g_udev;
static int g_configured;
static int g_running;
static int g_auto_period_sec = AUTO_PERIOD_DEFAULT;

static short vme_write(unsigned long addr, long data)
{
  return VME_write_32(g_udev, AM, (long)addr, data);
}

static short vme_read(unsigned long addr, long *data)
{
  return VME_read_32(g_udev, AM, (long)addr, data);
}

/* First VME access after open often times out; warm up USB/VME. */
static int usb_warmup(void)
{
  long junk = 0;
  short ret;

  xxusb_reset_toggle(g_udev);
  ret = vme_read(BASE + REG_ID, &junk);
  if (ret < 0) {
    xxusb_reset_toggle(g_udev);
    ret = vme_read(BASE + REG_ID, &junk);
  }
  return (ret < 0) ? -1 : 0;
}

static int identify(void)
{
  long id = 0;
  short ret;

  if (usb_warmup() < 0) {
    fprintf(stderr, "VME warm-up failed (timeout)\n");
    return -1;
  }

  ret = vme_read(BASE + REG_ID, &id);
  if (ret < 0) {
    xxusb_reset_toggle(g_udev);
    ret = vme_read(BASE + REG_ID, &id);
  }

  printf("ID ret=%d  ID=0x%08lx  base=0x%08lx  AM=0x%02x\n",
         ret, id, (unsigned long)BASE, AM);

  if (ret < 0 || (id & 0xffff0000UL) != 0x38200000UL) {
    fprintf(stderr, "No SIS3820 at base 0x%08lx\n", (unsigned long)BASE);
    return -1;
  }

  printf("SIS3820 found (firmware 0x%04lx)\n", id & 0xffffUL);
  return 0;
}

static void scaler_configure(void)
{
  vme_write(BASE + REG_ACQMODE, (long)ACQ_MODE_DEFAULT);
  vme_write(BASE + REG_INHIBIT, 0);   /* enable all channels */
  vme_write(BASE + KEY_FIFO_RST, 0);
  vme_write(BASE + KEY_CLEAR, 0);
  g_configured = 1;
  printf("Configured (input mode none; control pins do not inhibit).\n");
  printf("Non-clearing latch; use 'c' to clear counters.\n");
}

static void scaler_reset(void)
{
  vme_write(BASE + KEY_RESET, 0);
  g_running = 0;
  /* Re-apply safe mode immediately — bare KEY_RESET alone leaves
   * power-up defaults and feels like “r does nothing useful”. */
  scaler_configure();
  printf("Reset + reconfigured (S off until 'g').\n");
}

static void scaler_clear(void)
{
  vme_write(BASE + KEY_CLEAR, 0);
  printf("Counters cleared.\n");
}

static void scaler_start(void)
{
  if (!g_configured)
    scaler_configure();

  vme_write(BASE + KEY_ARM, 0);
  vme_write(BASE + KEY_ENABLE, 0);
  g_running = 1;
  printf("Started (S LED should be on).\n");
}

static void scaler_stop(void)
{
  vme_write(BASE + KEY_DISABLE, 0);
  g_running = 0;
  printf("Stopped (S LED should be off).\n");
}

/* Latch and read all 32 shadow channels into counts[]. */
static int scaler_read_counts(long counts[32])
{
  short ret;
  int i;
  int fails = 0;

  vme_write(BASE + KEY_LNE, 0);

  for (i = 0; i < 32; i++) {
    counts[i] = 0;
    ret = vme_read(BASE + SHADOW_BASE + (unsigned long)(4 * i), &counts[i]);
    if (ret < 0) {
      fails++;
      xxusb_reset_toggle(g_udev);
      ret = vme_read(BASE + SHADOW_BASE + (unsigned long)(4 * i), &counts[i]);
    }
  }
  return fails;
}

/*
 * Draw a fixed dashboard (cursor home + clear).
 * rates may be NULL (no rate column). auto_mode adds period / exit hint.
 */
static void scaler_draw_dashboard(const long counts[32],
                                  const double *rates,
                                  int fails,
                                  int auto_mode)
{
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char tbuf[32];
  int row, col, ch;

  if (tm)
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm);
  else
    snprintf(tbuf, sizeof(tbuf), "??:??:??");

  /* Move home and clear screen — updates overwrite in place. */
  fputs("\033[H\033[2J", stdout);

  printf("========== SIS3820 scaler @ 0x%08lx ==========\n",
         (unsigned long)BASE);
  printf("  time %s   running=%-3s   AM=0x%02x",
         tbuf, g_running ? "yes" : "no", AM);
  if (auto_mode)
    printf("   period=%ds", g_auto_period_sec);
  printf("\n");
  if (fails)
    printf("  (recovered %d VME timeouts this sample)\n", fails);
  printf("----------------------------------------------\n");

  /* 16 rows x 2 columns (ch1..32) */
  for (row = 0; row < 16; row++) {
    for (col = 0; col < 2; col++) {
      ch = row * 2 + col;
      if (rates)
        printf(" ch%02d %10lu %8.1f/s ",
               ch + 1, (unsigned long)counts[ch], rates[ch]);
      else
        printf(" ch%02d %10lu           ",
               ch + 1, (unsigned long)counts[ch]);
    }
    printf("\n");
  }

  printf("----------------------------------------------\n");
  if (auto_mode)
    printf("  AUTO  |  Enter = stop  |  rates = delta / period\n");
  else
    printf("  single sample\n");
  fflush(stdout);
}

static void scaler_monitor(void)
{
  long counts[32];
  int fails = scaler_read_counts(counts);
  scaler_draw_dashboard(counts, NULL, fails, 0);
  printf("\n");
}

static int stdin_ready(void)
{
  fd_set rfds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_SET(STDIN_FILENO, &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  return select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0;
}

/* Parse optional period after a command prefix of length prefix_len. */
static int set_auto_period_after_prefix(const char *line, int prefix_len)
{
  int sec;
  const char *p = line + prefix_len;

  while (*p == ' ' || *p == '\t')
    p++;
  if (*p == '\0' || *p == '\n')
    return 1;

  sec = atoi(p);
  if (sec < AUTO_PERIOD_MIN || sec > AUTO_PERIOD_MAX) {
    printf("Period must be %d..%d seconds (got %d).\n",
           AUTO_PERIOD_MIN, AUTO_PERIOD_MAX, sec);
    return 0;
  }
  g_auto_period_sec = sec;
  printf("Auto period set to %d s.\n", g_auto_period_sec);
  return 1;
}

/* Parse optional period after command letter; returns 1 if set ok / no arg. */
static int set_auto_period_from_line(const char *line)
{
  return set_auto_period_after_prefix(line, 1);
}

/* Auto-update dashboard in place until Enter. */
static void scaler_auto_monitor(void)
{
  char line[64];
  long counts[32];
  long prev[32];
  double rates[32];
  int have_prev = 0;
  int i;
  int fails;

  memset(prev, 0, sizeof(prev));
  memset(rates, 0, sizeof(rates));

  fputs("\033[?25l", stdout);   /* hide cursor */
  fflush(stdout);

  while (1) {
    fails = scaler_read_counts(counts);

    if (have_prev && g_auto_period_sec > 0) {
      for (i = 0; i < 32; i++) {
        long delta = counts[i] - prev[i];
        if (delta < 0)
          delta = 0;   /* wrap / clear */
        rates[i] = (double)delta / (double)g_auto_period_sec;
      }
    } else {
      for (i = 0; i < 32; i++)
        rates[i] = 0.0;
    }

    scaler_draw_dashboard(counts, have_prev ? rates : NULL, fails, 1);

    memcpy(prev, counts, sizeof(prev));
    have_prev = 1;

    {
      fd_set rfds;
      struct timeval tv;
      int n;

      FD_ZERO(&rfds);
      FD_SET(STDIN_FILENO, &rfds);
      tv.tv_sec = g_auto_period_sec;
      tv.tv_usec = 0;
      n = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
      if (n > 0) {
        fgets(line, sizeof(line), stdin);
        break;
      }
    }

    while (stdin_ready()) {
      fgets(line, sizeof(line), stdin);
      goto done;
    }
  }

done:
  fputs("\033[?25h", stdout);   /* show cursor */
  printf("\nAuto monitor stopped.\n");
  fflush(stdout);
}

/* clear + go + auto monitor (optional period: cga 2). */
static void scaler_cga(const char *line)
{
  if (!set_auto_period_after_prefix(line, 3))
    return;
  printf("cga: clear → start → auto (%d s)\n", g_auto_period_sec);
  scaler_clear();
  scaler_start();
  scaler_auto_monitor();
}

static void print_help(void)
{
  printf("\n");
  printf("  r  reset        full module reset (need g to run again)\n");
  printf("  g  go/start     configure (if needed), arm, enable\n");
  printf("  s  stop         disable counting\n");
  printf("  m  monitor      latch and print 32 channels once\n");
  printf("  a  [sec]        auto monitor (default period %d s; optional sec)\n",
         g_auto_period_sec);
  printf("  cga [sec]       clear + go + auto monitor\n");
  printf("  p  [sec]        show/set auto period only (current %d s)\n",
         g_auto_period_sec);
  printf("  c  clear        clear counters only\n");
  printf("  h  help\n");
  printf("  q  quit\n");
  printf("\n");
  printf("Examples:  a      start auto at current period\n");
  printf("           a 5    set period to 5 s and start auto\n");
  printf("           cga    clear, start, auto\n");
  printf("           cga 2  same with 2 s period\n");
  printf("           p 2    set period to 2 s without starting\n");
  printf("\n");
}

int main(void)
{
  xxusb_device_type devices[100];
  char line[64];
  char cmd;

  if (xxusb_devices_find(devices) < 1) {
    fprintf(stderr, "No VM-USB found\n");
    return 1;
  }

  g_udev = xxusb_device_open(devices[0].usbdev);
  if (!g_udev) {
    fprintf(stderr, "Failed to open VM-USB\n");
    return 1;
  }

  printf("Device open (%s)\n", devices[0].SerialString);

  if (identify() < 0) {
    xxusb_device_close(g_udev);
    return 1;
  }

  print_help();

  while (1) {
    printf("scaler> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin))
      break;

    if (line[0] == '\n' || line[0] == '\0')
      continue;

    /* Multi-letter command: cga [sec] */
    if (strncasecmp(line, "cga", 3) == 0 &&
        (line[3] == '\0' || line[3] == '\n' ||
         line[3] == ' ' || line[3] == '\t')) {
      scaler_cga(line);
      continue;
    }

    cmd = line[0];

    switch (cmd) {
    case 'r':
    case 'R':
      scaler_reset();
      break;
    case 'g':
    case 'G':
      scaler_start();
      break;
    case 's':
    case 'S':
      scaler_stop();
      break;
    case 'm':
    case 'M':
      scaler_monitor();
      break;
    case 'a':
    case 'A':
      if (set_auto_period_from_line(line))
        scaler_auto_monitor();
      break;
    case 'p':
    case 'P': {
      const char *p = line + 1;
      while (*p == ' ' || *p == '\t')
        p++;
      if (*p == '\0' || *p == '\n')
        printf("Auto period is %d s.\n", g_auto_period_sec);
      else
        set_auto_period_from_line(line);
      break;
    }
    case 'c':
    case 'C':
      scaler_clear();
      break;
    case 'h':
    case 'H':
    case '?':
      print_help();
      break;
    case 'q':
    case 'Q':
      if (g_running)
        scaler_stop();
      printf("Bye.\n");
      xxusb_device_close(g_udev);
      return 0;
    default:
      printf("Unknown command '%c' (h for help)\n", cmd);
      break;
    }
  }

  if (g_running)
    scaler_stop();
  xxusb_device_close(g_udev);
  return 0;
}
