#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/harness.c" <<'C'
#define _POSIX_C_SOURCE 200809L
#include "cdrom.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t i_stat, g_debug_current_func_addr, g_debug_last_store_pc;
uint64_t s_frame_count;
static uint8_t ram[2 * 1024 * 1024];
static int dirty_calls; static uint32_t dirty_phys, dirty_len;
uint8_t *memory_get_ram_ptr(void){return ram;}
void dirty_ram_mark_executable_range(uint32_t p,uint32_t l){dirty_calls++;dirty_phys=p;dirty_len=l;}
void psx_irq_raise(uint32_t b,uint32_t d){(void)d;i_stat|=b;}
void event_ring_record(uint32_t a,uint32_t b,uint32_t c){(void)a;(void)b;(void)c;}
void event_ring_record_aux(uint32_t a,uint32_t b,uint32_t c,uint32_t d){(void)a;(void)b;(void)c;(void)d;}
void audio_trace_event(uint32_t a,uint32_t b,uint32_t c,uint32_t d){(void)a;(void)b;(void)c;(void)d;}
int dma_cdrom_transfer_active(void){return 0;}
void spu_cd_audio_reset(void){}
void spu_cd_audio_push(const int16_t*p,int n){(void)p;(void)n;}
void *iso_open(const char*p){(void)p;return(void*)1;}
int iso_read_sector(void*h,uint32_t l,uint8_t*b,int s){(void)h;(void)l;memset(b,0,(size_t)s);return 1;}
int iso_read_raw_sector(void*h,uint32_t l,uint8_t*b,int s){(void)h;(void)l;memset(b,0,(size_t)s);return 1;}
uint32_t iso_sector_count(void*h){(void)h;return 220184u;}
void iso_close(void*h){(void)h;}
int iso_track_count(void*h){(void)h;return 1;}
uint32_t iso_track_start_lba(void*h,int t){(void)h;(void)t;return 0;}
uint32_t iso_track_pregap_lba(void*h,int t){(void)h;(void)t;return 0;}
int iso_track_is_audio(void*h,int t){(void)h;(void)t;return 0;}
static void idx(uint8_t x){cdrom_write(0x1F801800u,x);}
static void ack(void){idx(1);cdrom_write(0x1F801803u,7);idx(0);i_stat=0;}
static void par(uint8_t x){idx(0);cdrom_write(0x1F801802u,x);}
static void cmd(uint8_t x){idx(0);cdrom_write(0x1F801801u,x);}
static uint32_t rd32(const uint8_t*p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void wr32(uint8_t*p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void die(const char*s){fprintf(stderr,"FAIL: %s\n",s);exit(1);}
int main(void){
 setenv("PSX_FM_APV2_BYPASS","1",1);setenv("PSX_CD_APV2_STOCK","1",1);cdrom_init("dummy.cue");
 wr32(ram+0x168184,0x2C620014u);wr32(ram+0x168188,0x1040023Au);wr32(ram+0x16818C,0x00031080u);
 s_frame_count=100;cdrom_advance(1);
 if(rd32(ram+0x168188)!=0x1000023Au)die("signature bypass word");
 if(dirty_calls!=1||dirty_phys!=0x168188u||dirty_len!=4u)die("dirty executable mark");
 par(0x24);par(0x26);par(0x00);cmd(0x02);ack();cmd(0x0B);ack();cmd(0x03);
 CDROMDebugState st;cdrom_debug_snapshot(&st);
 if(st.apv2_probe_play_acks!=1||st.irq_flag!=3)die("AP probe Play ACK");
 ack();
 par(0x04);cmd(0x19);cdrom_debug_snapshot(&st);
 if(!st.apv2_probe_active||st.apv2_test04_count!=1||st.irq_flag!=3)die("Test04");
 ack();
 par(0x05);cmd(0x19);cdrom_debug_snapshot(&st);
 if(st.apv2_probe_active||st.apv2_test05_count!=1||st.response_count!=2||st.irq_flag!=3)die("Test05");
 idx(0);uint8_t total=(uint8_t)cdrom_read(0x1F801801u),success=(uint8_t)cdrom_read(0x1F801801u);
 if(total||success)die("SCEx result");
 puts("PASS: signature bypass, AP probe Play ACK, Test04 reset, Test05 00/00");return 0;
}
C
gcc -std=gnu99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -I"$ROOT/psxrecomp/runtime/include" -I"$ROOT/psxrecomp/runtime/src" \
  "$TMP/harness.c" "$ROOT/psxrecomp/runtime/src/cdrom.c" -o "$TMP/harness"
"$TMP/harness"
