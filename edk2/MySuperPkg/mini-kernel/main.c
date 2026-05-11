#include "processor_init.inc"

typedef unsigned char u8;
typedef unsigned long long u64;
typedef unsigned u32;

#include "font.inc"

typedef struct _VideoBufferInfo {
  u64 buffer;
  u64 height;
  u64 width;
} __attribute__((packed)) VideoBufferInfo;

typedef struct _ProcessorsInfo {
  u8 size;
  u8 ids[256];
} __attribute__((packed)) ProcessorsInfo;

typedef struct _SystemInfo {
  VideoBufferInfo video;
  ProcessorsInfo processors;
} __attribute__((packed)) SystemInfo;

SystemInfo *info;

u8 get_self_apic_id(void);

#define STATUS_BUFFER_SIZE 2048

#define COLOR_BACKGROUND 0x00000000
#define COLOR_TEXT       0x0000FF00

#define STATUS_X 100
#define STATUS_Y 120
#define TEXT_LINE_GAP 8

static volatile u8 cpu_alive[256];
static volatile u8 ap_startup_was_called = 0;

void screen_clear(VideoBufferInfo *video, u32 color) {
  u64 h = info->video.height;
  u64 w = info->video.width;
  volatile u32 *b = (u32 *)info->video.buffer;

  for (u64 i = 0; i < h; ++i) {
    for (u64 j = 0; j < w; ++j) {
      b[i * w + j] = color;
    }
  }
}

void screen_set_pixel(VideoBufferInfo *video, u64 x, u64 y, u32 color) {
  u64 w = info->video.width;
  volatile u32 *b = (u32 *)info->video.buffer;

  b[y * w + x] = color;
}

void screen_draw_char(VideoBufferInfo *video, u64 x, u64 y, u8 c) {
  u8 *bits = &font[c][0];

  for (u64 i = 0; i < FONT_HEIGHT; ++i) {
    for (u64 j = 0; j < FONT_WIDTH; ++j) {
      u32 n = i * FONT_WIDTH + j;
      u8 b = bits[n / 8] >> (n % 8) & 1;

      if (b == 1)
        screen_set_pixel(video, x + j, y + i, COLOR_TEXT);
    }
  }
}

void screen_draw_string(VideoBufferInfo *video, u64 x, u64 y, const char *s) {
  while (*s != '\0') {
    screen_draw_char(video, x, y, *s);
    x += FONT_WIDTH;
    ++s;
  }
}


void screen_draw_text(VideoBufferInfo *video, u64 x, u64 y, const char *s) {
  u64 start_x = x;

  while (*s != '\0') {
    if (*s == '\n') {
      x = start_x;
      y += FONT_HEIGHT + TEXT_LINE_GAP;
      ++s;
      continue;
    }

    screen_draw_char(video, x, y, *s);
    x += FONT_WIDTH;
    ++s;
  }
}

void mark_cpu_alive(u8 apic_id) {
  cpu_alive[apic_id] = 1;
}

void mark_current_cpu_alive(void) {
  mark_cpu_alive(get_self_apic_id());
}

u8 count_alive_processors(void) {
  u8 alive = 0;
  u8 total = info->processors.size;

  for (u8 i = 0; i < total; ++i) {
    u8 apic_id = info->processors.ids[i];

    if (cpu_alive[apic_id]) {
      ++alive;
    }
  }

  return alive;
}

static char status_buffer[STATUS_BUFFER_SIZE];

void build_kernel_status_buffer(void) {
  u64 pos = 0;

#define BUF_PUTC(ch)                                      \
  do {                                                    \
    if (pos + 1 < STATUS_BUFFER_SIZE) {                   \
      status_buffer[pos] = (char)(ch);                    \
      ++pos;                                              \
      status_buffer[pos] = '\0';                          \
    }                                                     \
  } while (0)

#define BUF_PUTS(str)                                     \
  do {                                                    \
    char *src = (char *)(str);                            \
    for (u64 i = 0; src[i] != '\0'; ++i) {                \
      BUF_PUTC(src[i]);                                   \
    }                                                     \
  } while (0)

#define BUF_PUT_DEC(value_expr)                           \
  do {                                                    \
    u64 value = (u64)(value_expr);                        \
    u64 divisor = 1;                                      \
                                                          \
    while (value / divisor >= 10) {                       \
      divisor *= 10;                                      \
    }                                                     \
                                                          \
    while (divisor > 0) {                                 \
      u8 digit = (u8)(value / divisor);                   \
      BUF_PUTC((char)('0' + digit));                      \
      value = value % divisor;                            \
      divisor = divisor / 10;                             \
    }                                                     \
  } while (0)

#define BUF_PUT_HEX(value_expr)                           \
  do {                                                    \
    u64 value = (u64)(value_expr);                        \
    BUF_PUTS("0x");                                      \
                                                          \
    for (int shift = 60; shift >= 0; shift -= 4) {        \
      u8 digit = (u8)((value >> shift) & 0xF);            \
                                                          \
      if (digit < 10) {                                   \
        BUF_PUTC((char)('0' + digit));                    \
      } else {                                            \
        BUF_PUTC((char)('A' + digit - 10));               \
      }                                                   \
    }                                                     \
  } while (0)

  BUF_PUTS("MINI-KERNEL STATUS\n");
  BUF_PUTS("------------------\n");
//  BUF_PUTS("BOOT PATH: UEFI -> EXIT BOOT SERVICES -> KERNEL\n");

  BUF_PUTS("KERNEL ENTRY: ");
  BUF_PUT_HEX(0x2000);
  BUF_PUTC('\n');

  BUF_PUTS("AP TRAMPOLINE: ");
  BUF_PUT_HEX(0x1000);
  BUF_PUTC('\n');

  BUF_PUTS("BSP STACK: ");
  BUF_PUT_HEX(0x100000);
  BUF_PUTC('\n');

  BUF_PUTS("SYSTEMINFO: ");
  BUF_PUT_HEX((u64)info);
  BUF_PUTC('\n');

  BUF_PUTS("FRAMEBUFFER: ");
  BUF_PUT_HEX(info->video.buffer);
  BUF_PUTC('\n');

  BUF_PUTS("BUFFER LOGGER: OK\n");
//  BUF_PUTS("MULTILINE TEXT: OK\n");

  BUF_PUTS("WIDTH: ");
  BUF_PUT_DEC(info->video.width);
  BUF_PUTC('\n');

  BUF_PUTS("HEIGHT: ");
  BUF_PUT_DEC(info->video.height);
  BUF_PUTC('\n');

  BUF_PUTS("CPU COUNT: ");
  BUF_PUT_DEC(count_alive_processors());
  BUF_PUTC('/');
  BUF_PUT_DEC(info->processors.size);
  BUF_PUTC('\n');

  BUF_PUTS("AP STARTUP: ");

  if (ap_startup_was_called) {
    BUF_PUTS("ENABLED");
  } else {
    BUF_PUTS("DISABLED");
  }

BUF_PUTC('\n');

  BUF_PUTS("BSP APIC ID: ");
  BUF_PUT_DEC(get_self_apic_id());
  BUF_PUTC('\n');

  BUF_PUTS("APIC IDS: ");

  u8 cpu_count = info->processors.size;

  for (u8 i = 0; i < cpu_count; ++i) {
    BUF_PUT_DEC(info->processors.ids[i]);

    if ((u8)(i + 1) < cpu_count) {
      BUF_PUTC(' ');
    }
  }

  BUF_PUTC('\n');

//  BUF_PUTS("HEX FORMATTER: OK\n");
  BUF_PUTS("APIC IDS: OK\n");
  BUF_PUTS("BSP APIC ID: OK\n");
  BUF_PUTS("STATUS: OK\n");

#undef BUF_PUT_HEX
#undef BUF_PUT_DEC
#undef BUF_PUTS
#undef BUF_PUTC
}

void start_ap(u8 ap_apic_id, u32 init_code_entry) {
  volatile u32 *const APIC_ICR_LOW = (void *)0xfee00300;
  volatile u32 *const APIC_ICR_HIG = (void *)0xfee00310;

  // INIT
  *APIC_ICR_HIG = (u32)ap_apic_id << 24;
  *APIC_ICR_LOW = 0x00004500;

  while (*APIC_ICR_LOW & (1 << 12))
    ; // Ждем окончания доставки

  for (volatile u32 i = 0xFFFFFF; i; --i)
    ;

  // SIPI
  *APIC_ICR_HIG = (u32)ap_apic_id << 24;
  *APIC_ICR_LOW = ((u32)0x00004600 | (init_code_entry >> 12));
}

void startup_processors(void) {
  // *(volatile u32 *) 0x310 = 0xdedababa;

  for (u32 i = 0; i < processor_init_len; ++i) {
    ((volatile u8 *)0x1000)[i] = processor_init[i];
  }

  u64 cr3 = 0;

  __asm volatile("movq %%cr3, %0\n\t" : "=r"(cr3) : :);

  *(volatile u64 *)((u8 *)0x1000 + processor_init_len - 16) = cr3;
  *(volatile u64 *)((u8 *)0x1000 + processor_init_len - 8) = 0x2000;

  u8 count = info->processors.size;

  for (u8 i = 1; i < count; ++i) {
    u8 ap_apic_id = info->processors.ids[i];
    start_ap(ap_apic_id, 0x1000);
  }
}

u8 get_self_apic_id(void) {
  u32 apic_id = *((volatile u32 *)0xfee00020);
  return apic_id >> 24;
}

int main() {
  // if (get_self_apic_id() == 0) {
  //   startup_processors();
  // }

  mark_current_cpu_alive();

  screen_clear(&info->video, COLOR_BACKGROUND);

  // screen_draw_char(&info->video, 100, 100, '&');

  build_kernel_status_buffer();

  screen_draw_text(&info->video, STATUS_X, STATUS_Y, status_buffer);

  // u64 w = info->video.width;
  // u64 h = info->video.height;
  // u32 *b = (u32 *)info->video.buffer;
  //
  // u32 self_apic_id = get_self_apic_id();
  //
  // for (u32 color = 0;; color = ((color + 1) & 0x00ffffff)) {
  //   for (u64 i = self_apic_id * 10; i < (self_apic_id + 1) * 10; ++i) {
  //     for (u64 j = 0; j < w; ++j) {
  //       b[i * w + j] = color;
  //     }
  //   }
  // }

  return 0;
}
