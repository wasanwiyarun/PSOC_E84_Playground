#include <string.h>
#include <zephyr/console/console.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#define BLOCK 262144U
#define SIZE 256U
static uint8_t pattern[SIZE]; static uint32_t pattern_crc; static bool written;
static int open_area(const struct flash_area **a) { int rc=flash_area_open(FIXED_PARTITION_ID(storage_partition),a); if(rc) printk("ERR FLASH OPEN=%d\n",rc); return rc; }
static void flash_info(void) { const struct flash_area *a; if(!open_area(&a)) { printk("OK FLASH DEVICE=%s PARTITION=storage OFFSET=%ld SIZE=%ld ERASE_BLOCK=%u\n",a->fa_dev->name,(long)a->fa_off,(long)a->fa_size,BLOCK); flash_area_close(a); } }
static void write_data(void) { const struct flash_area *a; int rc; if(open_area(&a)) return; for(size_t i=0;i<SIZE;i++) pattern[i]=0xa5U^(uint8_t)i; pattern_crc=crc32_ieee(pattern,SIZE); rc=flash_area_erase(a,0,BLOCK); if(!rc) rc=flash_area_write(a,0,pattern,SIZE); if(rc) printk("ERR FLASH WRITE=%d\n",rc); else { written=true; printk("OK FLASH WRITE OFFSET=0 SIZE=%u CRC=0x%08x\n",SIZE,pattern_crc); } flash_area_close(a); }
static void read_data(void) { const struct flash_area *a; uint8_t data[SIZE]; int rc; uint32_t crc; if(!written) { printk("ERR FLASH READ=NO_PATTERN_WRITTEN\n"); return; } if(open_area(&a)) return; rc=flash_area_read(a,0,data,SIZE); crc=crc32_ieee(data,SIZE); if(!rc && crc==pattern_crc && !memcmp(pattern,data,SIZE)) printk("PASS FLASH READ OFFSET=0 SIZE=%u CRC=0x%08x\n",SIZE,crc); else printk("ERR FLASH READ=%d CRC=0x%08x\n",rc,crc); flash_area_close(a); }
int main(void) { char cmd[32]; size_t n=0; console_init(); flash_info(); printk("INFO COMMANDS=flash info;flash write;flash read;help\n"); for(;;) { uint8_t c=console_getchar(); if(c=='\r'||c=='\n') { if(!n) continue; cmd[n]=0; if(!strcmp(cmd,"flash info")) flash_info(); else if(!strcmp(cmd,"flash write")) write_data(); else if(!strcmp(cmd,"flash read")) read_data(); else if(!strcmp(cmd,"help")) printk("INFO COMMANDS=flash info;flash write;flash read;help\n"); else printk("ERR UNSUPPORTED %s\n",cmd); n=0; } else if(n<sizeof(cmd)-1) cmd[n++]=(char)c; else n=0; } }
