#include "processor_init.inc"

typedef unsigned char u8;
typedef unsigned long long u64;
typedef unsigned u32;

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
        screen_set_pixel(video, x + j, y + i, 0x0000FF00);
    }
  }
}

void screen_draw_string(VideoBufferInfo *video, u64 x, u64 y, char *s) {
  while (*s != '\0') {
    screen_draw_char(video, x, y, *s);
    x += FONT_WIDTH;
    ++s;
  }
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

  screen_clear(&info->video, 0x00000000);

  // screen_draw_char(&info->video, 100, 100, '&');

  screen_draw_string(&info->video, 100, 200, "Hello World!!!! &!@#$");

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
