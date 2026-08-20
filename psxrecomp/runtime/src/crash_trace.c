/* crash_trace.c — unified crash diagnostic dump.
 *
 * Writes a single JSON report file (psx_last_run_report.json) on:
 *   - signal (SIGSEGV / SIGABRT)
 *   - Windows SEH unhandled exception
 *   - atexit
 *   - fail-fast psx_unknown_dispatch
 *   - trap_crash
 *   - TCP "post_mortem_dump" command (future)
 *
 * Mirrors the sibling SuperMarioWorldRecomp project's src/post_mortem.c. The file
 * is OVERWRITTEN on each dump (last-write-wins, single file per run);
 * this is not a log per DEVELOPMENT.md §3 — it's a one-shot final state
 * snapshot for crashes the running TCP server cannot intercept.
 *
 * All payload comes from already-existing rings; this module is a
 * serializer, not a recorder.
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>     /* __readgsqword — fiber TEB stack bounds for native_stack walk */
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>   /* va_start/va_end — not pulled in transitively off Windows */
#include <time.h>

#include "cpu_state.h"
#include "crash_trace.h"
#include "cdrom.h"
#include "spu.h"

/* Output path — overwritten per dump. */
static const char *kReportPath = "psx_last_run_report.json";

/* Build identity, embedded into every report so a user-submitted crash can be
 * correlated to an exact build (issue #1 reports had no version field). The git
 * rev comes from runtime.cmake (PSX_BUILD_REV); __DATE__/__TIME__ are a always-
 * available fallback that still distinguishes builds. */
#ifndef PSX_BUILD_REV
#define PSX_BUILD_REV "unknown"
#endif
static const char *kBuildId = PSX_BUILD_REV " (" __DATE__ " " __TIME__ ")";

/* CPU state pointer (set by debug server at init). */
extern CPUState *debug_cpu_ptr;

/* Frame counter from debug_server.c (non-static). */
extern uint64_t s_frame_count;

/* Globals from debug_server.c we want to capture. */
extern uint32_t g_debug_current_func_addr;
extern uint32_t g_debug_last_store_pc;

/* Native dispatch nesting depth (generated/SCPH1001_dispatch.c). Incremented per
 * nested psx_dispatch_call, decremented on return. A huge value at crash time is
 * the direct fingerprint of runaway recursion (the host C stack mirrors the guest
 * call graph, so an unbounded call chain overflows it -> STATUS_STACK_OVERFLOW
 * 0xC00000FD). Pairs with the dirty_block tail, which names the recursing PCs. */
extern int g_psx_dispatch_depth;

/* Dispatch ring — accessor wrappers exported by debug_server.c. */
#define DISPATCH_TRACE_CAP (1 << 16)
extern uint32_t crash_trace_dispatch_ring_get(int idx);
extern uint64_t crash_trace_dispatch_seq_get(void);

/* Unknown-dispatch ring — layout must match debug_server.c's
 * UnknownDispatchEntry. Accessor wrappers exported by debug_server.c. */
typedef struct {
    uint64_t seq;
    uint32_t addr, phys, ra, a0, a1, frame, pad;
} UnknownDispatchEntry;
#define UNKNOWN_DISPATCH_CAP (1 << 16)
extern UnknownDispatchEntry crash_trace_unknown_get(uint64_t seq);
extern uint64_t crash_trace_unknown_seq_get(void);

/* Dirty-RAM block log (defined in dirty_ram_interp.c). */
#include "dirty_ram_interp.h"

/* JSON helpers. Hand-rolled to avoid allocations on the SEH path. */

static int append_str(char *buf, size_t cap, size_t *pos, const char *s) {
    size_t n = strlen(s);
    if (*pos + n >= cap) return 0;
    memcpy(buf + *pos, s, n);
    *pos += n;
    buf[*pos] = 0;
    return 1;
}

static int append_fmt(char *buf, size_t cap, size_t *pos, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *pos) return 0;
    *pos += (size_t)n;
    return 1;
}

/* Escape a string for use as a JSON string value: backslash and double-quote
 * are backslash-escaped, control chars are dropped. Windows module paths
 * (e.g. C:\Games\...\TombaRecomp.exe) otherwise emit invalid JSON that parsers
 * reject — which is exactly what happened to a user-submitted crash report. */
static void json_escape(const char *in, char *out, size_t outcap) {
    size_t o = 0;
    for (size_t i = 0; in && in[i] && o + 2 < outcap; i++) {
        unsigned char ch = (unsigned char)in[i];
        if (ch == '\\' || ch == '"') { out[o++] = '\\'; out[o++] = (char)ch; }
        else if (ch >= 0x20)         { out[o++] = (char)ch; }
    }
    out[o] = 0;
}

/* Serialize a single uint32_t hex value as a JSON string. */
static void hex32(char *out, uint32_t v) {
    snprintf(out, 16, "\"0x%08X\"", v);
}

/* ── Exit origin ─────────────────────────────────────────────────────── */

/* Deliberate exit() callers tag themselves here so an "atexit" report can
 * distinguish a TCP quit from an SDL window close from an unexplained
 * exit.  "unknown" in a report means NOBODY tagged — main returned or an
 * untagged exit() fired; that is a finding, not noise. */
static const char *s_exit_origin = "unknown";

void psx_crash_trace_set_exit_origin(const char *origin) {
    if (origin) s_exit_origin = origin;
}

/* ── Native call-stack snapshot ──────────────────────────────────────────
 *
 * The recent_fn ring is TIME-ORDERED (function entries over time), so for a
 * runaway recursion its tail is dominated by whatever leaf was churning at the
 * trip — it does NOT show the actual recursion CYCLE. This walks the running
 * fiber's native stack at the fatal instant and recovers the true cycle:
 *
 *   - Walk from the current/faulting RSP up to the fiber StackBase (TEB GS:[8]).
 *   - Keep only qwords that are genuine return addresses: a value pointing into
 *     this module's .text whose immediately-preceding bytes are a `call`
 *     instruction (filters spilled function pointers / stale data that merely
 *     look like code addresses).
 *   - Run-length-collapse consecutive equal frames and emit module-relative
 *     RVAs, plus a small frequency histogram (the recursion participants each
 *     appear ~depth times and dominate it).
 *
 * Emitted RVAs are build-relative (module base + RVA). Symbolize offline against
 * the exact binary with nm (see _freeze_specimens/analyze_named.py, which reads
 * this `native_stack` block directly). Works on BOTH the SEH path (uses the
 * faulting ContextRecord->Rsp) and the graceful stack-guard halt path (walks the
 * current frame), in debug and release — no minidump required. */
#ifdef _WIN32
static int crash_is_retaddr(uintptr_t v, uintptr_t text_lo, uintptr_t text_hi) {
    if (v < text_lo + 7u || v >= text_hi) return 0;
    const unsigned char *p = (const unsigned char *)(v - 7);
    if (p[2] == 0xE8) return 1;                       /* call rel32  -> E8 at v-5 */
    unsigned char m = p[5];                            /* byte at v-2 */
    if (p[4] == 0xFF && ((m >= 0xD0 && m <= 0xD7) ||   /* call reg            */
                         (m >= 0x10 && m <= 0x17)))    /* call [reg]          */
        return 1;
    if (p[1] == 0xFF && p[2] == 0x15) return 1;        /* call [rip+disp32]   */
    if (p[3] == 0xFF && p[4] >= 0x50 && p[4] <= 0x57)  /* call [reg+disp8]    */
        return 1;
    return 0;
}

static void append_native_stack(char *buf, size_t cap, size_t *pos, uintptr_t start_sp) {
    HMODULE h = GetModuleHandleW(NULL);
    uintptr_t mb = (uintptr_t)h;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)h;
    IMAGE_NT_HEADERS *nt  = (IMAGE_NT_HEADERS *)((BYTE *)h + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    uintptr_t text_lo = 0, text_hi = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!memcmp(sec[i].Name, ".text", 5)) {
            text_lo = mb + sec[i].VirtualAddress;
            text_hi = text_lo + sec[i].Misc.VirtualSize;
            break;
        }
    }
    uintptr_t base = (uintptr_t)__readgsqword(0x08);   /* fiber StackBase (high) */
    char probe;
    if (start_sp == 0) start_sp = (uintptr_t)&probe;
    start_sp &= ~(uintptr_t)7;

    append_fmt(buf, cap, pos,
        "  \"native_stack\": {\n"
        "    \"module_base\": \"0x%llX\",\n"
        "    \"text_lo_rva\": \"0x%llX\",\n"
        "    \"text_hi_rva\": \"0x%llX\",\n"
        "    \"frames\": [",
        (unsigned long long)mb,
        (unsigned long long)(text_lo - mb),
        (unsigned long long)(text_hi - mb));

    enum { HCAP = 24, MAX_RUNS = 256 };
    uint32_t hk[HCAP]; uint32_t hc[HCAP]; int hn = 0;
    uint32_t prev_rva = 0; int run = 0, emitted = 0, total = 0;

    /* This runs in the crash/halt path, so it MUST never fault: bound every
     * read with VirtualQuery and stop at the first non-committed / non-readable
     * page or the PAGE_GUARD page (touching it would re-arm the overflow). The
     * fiber StackBase (base) is only a hint — if it's stale/bogus the region
     * walk terminates safely at the real committed-stack top anyway. */
    if (text_lo && start_sp) {
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t region_end = 0;
        for (uintptr_t a = start_sp; total < 300000; a += 8) {
            if (base > start_sp && a + 8 > base) break;       /* don't pass a good StackBase */
            if (a + 8 > region_end) {                          /* (re)validate the page region */
                if (!VirtualQuery((void *)a, &mbi, sizeof(mbi))) break;
                if (!(mbi.State & MEM_COMMIT)) break;
                if (mbi.Protect & PAGE_GUARD) break;
                DWORD pr = mbi.Protect & 0xFFu;
                if (!(pr & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                    break;
                region_end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            }
            uintptr_t v = *(uintptr_t *)a;
            if (!crash_is_retaddr(v, text_lo, text_hi)) continue;
            uint32_t rva = (uint32_t)(v - mb);
            total++;
            int found = 0;
            for (int k = 0; k < hn; k++) { if (hk[k] == rva) { hc[k]++; found = 1; break; } }
            if (!found && hn < HCAP) { hk[hn] = rva; hc[hn] = 1; hn++; }
            if (total > 1 && rva == prev_rva) { run++; continue; }
            if (run > 0 && emitted < MAX_RUNS) {
                append_fmt(buf, cap, pos, "%s{\"rva\":\"0x%X\",\"n\":%d}",
                           emitted ? "," : "", prev_rva, run);
                emitted++;
            }
            prev_rva = rva; run = 1;
        }
        if (run > 0 && emitted < MAX_RUNS)
            append_fmt(buf, cap, pos, "%s{\"rva\":\"0x%X\",\"n\":%d}",
                       emitted ? "," : "", prev_rva, run);
    }
    append_fmt(buf, cap, pos,
        "],\n    \"total_frames\": %d,\n    \"runs_emitted\": %d,\n    \"histogram\": [",
        total, emitted);
    for (int k = 0; k < hn; k++)
        append_fmt(buf, cap, pos, "%s{\"rva\":\"0x%X\",\"n\":%u}", k ? "," : "", hk[k], hc[k]);
    append_str(buf, cap, pos, "]\n  },\n");
}
#endif /* _WIN32 */

/* ── Main entry ──────────────────────────────────────────────────────── */

void psx_crash_trace_dump(const char *reason, void *seh_info) {
    /* Pre-allocate large stack buffer; avoid heap on SEH path. */
    static char buf[8 * 1024 * 1024]; /* 8 MB */
    size_t pos = 0;

    /* Header */
    char ts[64] = {0};
    {
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        if (tm) strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);
    }

    append_fmt(buf, sizeof(buf), &pos,
        "{\n"
        "  \"reason\": \"%s\",\n"
        "  \"exit_origin\": \"%s\",\n"
        "  \"build\": \"%s\",\n"
        "  \"timestamp\": \"%s\",\n"
        "  \"frame\": %llu,\n"
        "  \"dispatch_depth\": %d,\n"
        "  \"last_func_addr\": \"0x%08X\",\n"
        "  \"last_store_pc\": \"0x%08X\",\n",
        reason ? reason : "(unknown)",
        s_exit_origin,
        kBuildId,
        ts,
        (unsigned long long)s_frame_count,
        g_psx_dispatch_depth,
        g_debug_current_func_addr,
        g_debug_last_store_pc);

#ifdef _WIN32
    if (seh_info) {
        EXCEPTION_POINTERS *info = (EXCEPTION_POINTERS *)seh_info;
        DWORD code = info->ExceptionRecord->ExceptionCode;
        void *addr = info->ExceptionRecord->ExceptionAddress;
        const char *kind = "?";
        ULONG_PTR fault_addr = 0;
        if (code == EXCEPTION_ACCESS_VIOLATION) {
            ULONG_PTR k = info->ExceptionRecord->ExceptionInformation[0];
            kind = (k == 0) ? "read" : (k == 1) ? "write" : "execute";
            fault_addr = info->ExceptionRecord->ExceptionInformation[1];
        }
        append_fmt(buf, sizeof(buf), &pos,
            "  \"seh\": {\n"
            "    \"code\": \"0x%08lX\",\n"
            "    \"address\": \"%p\",\n"
            "    \"access\": \"%s\",\n"
            "    \"fault_addr\": \"0x%p\",\n",
            code, addr, kind, (void *)fault_addr);

        /* Module-relative location of the faulting instruction, so the
         * address survives ASLR and feeds straight into addr2line. */
        {
            MEMORY_BASIC_INFORMATION mbi;
            char mod_name[MAX_PATH] = "?";
            void *mod_base = NULL;
            if (VirtualQuery(addr, &mbi, sizeof(mbi))) {
                mod_base = mbi.AllocationBase;
                GetModuleFileNameA((HMODULE)mod_base, mod_name, sizeof(mod_name));
            }
            char mod_esc[MAX_PATH * 2];
            json_escape(mod_name, mod_esc, sizeof(mod_esc));
            append_fmt(buf, sizeof(buf), &pos,
                "    \"module\": \"%s\",\n"
                "    \"module_base\": \"%p\",\n"
                "    \"module_offset\": \"0x%llX\",\n",
                mod_esc, mod_base,
                (unsigned long long)((char *)addr - (char *)mod_base));
        }

        /* Poor-man's backtrace: scan the faulting thread's stack for values
         * that point into executable module regions and report them
         * module-relative. Noisy but enough to pin the faulting call path. */
        append_str(buf, sizeof(buf), &pos, "    \"stack_scan\": [");
        {
            CONTEXT *ctx = info->ContextRecord;
            ULONG_PTR *sp = (ULONG_PTR *)ctx->Rsp;
            int emitted = 0;
            for (int i = 0; i < 512 && emitted < 24; i++) {
                ULONG_PTR v;
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(&sp[i], &mbi, sizeof(mbi)) ||
                    !(mbi.State & MEM_COMMIT)) break;
                v = sp[i];
                if (v < 0x10000) continue;
                if (!VirtualQuery((void *)v, &mbi, sizeof(mbi))) continue;
                if (mbi.Type != MEM_IMAGE) continue;
                if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                     PAGE_EXECUTE_READWRITE))) continue;
                char mod_name[MAX_PATH] = "?";
                GetModuleFileNameA((HMODULE)mbi.AllocationBase, mod_name,
                                   sizeof(mod_name));
                const char *base = strrchr(mod_name, '\\');
                char bn_esc[MAX_PATH * 2];
                json_escape(base ? base + 1 : mod_name, bn_esc, sizeof(bn_esc));
                append_fmt(buf, sizeof(buf), &pos,
                    "%s\n      {\"module\": \"%s\", \"offset\": \"0x%llX\"}",
                    emitted ? "," : "", bn_esc,
                    (unsigned long long)(v - (ULONG_PTR)mbi.AllocationBase));
                emitted++;
            }
        }
        append_str(buf, sizeof(buf), &pos, "\n    ]\n  },\n");
    }
#else
    (void)seh_info;
#endif

    /* CPU state */
    if (debug_cpu_ptr) {
        CPUState *cpu = debug_cpu_ptr;
        append_str(buf, sizeof(buf), &pos, "  \"cpu\": {\n");
        append_fmt(buf, sizeof(buf), &pos,
            "    \"pc\": \"0x%08X\",\n"
            "    \"hi\": \"0x%08X\",\n"
            "    \"lo\": \"0x%08X\",\n"
            "    \"sr\": \"0x%08X\",\n"
            "    \"cause\": \"0x%08X\",\n"
            "    \"epc\": \"0x%08X\",\n"
            "    \"gpr\": [",
            cpu->pc, cpu->hi, cpu->lo,
            cpu->cop0[12], cpu->cop0[13], cpu->cop0[14]);
        for (int i = 0; i < 32; i++) {
            append_fmt(buf, sizeof(buf), &pos,
                "%s\"0x%08X\"", i == 0 ? "" : ",", cpu->gpr[i]);
        }
        append_str(buf, sizeof(buf), &pos, "]\n  },\n");
    } else {
        append_str(buf, sizeof(buf), &pos, "  \"cpu\": null,\n");
    }

    /* SPU state is small enough to retain in every normal-close report. This
     * makes intermittent menu/dialog SFX failures diagnosable without a live
     * TCP capture: we can see whether voices were keyed on and whether the
     * game programmed hardware sweep channels. */
    {
        SpuDebugInfo s;
        spu_debug_info(&s);
        append_fmt(buf, sizeof(buf), &pos,
            "  \"spu_state\": {"
            "\"ctrl\":\"0x%04X\",\"active_mask\":\"0x%06X\","
            "\"main_l\":%d,\"main_r\":%d,"
            "\"cd_l\":%d,\"cd_r\":%d,"
            "\"key_on_count\":%u,\"sweep_active_channels\":%u,"
            "\"render_frames\":%llu,\"nonzero_frames\":%llu,"
            "\"last_peak\":%d,\"peak\":%d,"
            "\"cd_frames\":%u,\"cd_push_frames\":%llu,"
            "\"cd_overflow_frames\":%llu,"
            "\"cd_underflow_frames\":%llu},\n",
            s.ctrl & 0xFFFFu, s.active_mask & 0xFFFFFFu,
            s.main_l, s.main_r, s.cd_l, s.cd_r,
            s.key_on_count, s.sweep_active_channels,
            (unsigned long long)s.render_frames,
            (unsigned long long)s.nonzero_frames,
            s.last_peak, s.peak, s.cd_frames,
            (unsigned long long)s.cd_push_frames,
            (unsigned long long)s.cd_overflow_frames,
            (unsigned long long)s.cd_underflow_frames);
    }

    /* CD-ROM post-mortem state. Forbidden Memories performs a second libcd
     * initialization after its publisher logo; a live wait there is not a CPU
     * crash, so the normal atexit report must retain the controller state and
     * the command/sector history needed to distinguish timing, IRQ and media
     * delivery failures. */
    {
        CDROMDebugState s;
        cdrom_debug_snapshot(&s);
        append_fmt(buf, sizeof(buf), &pos,
            "  \"cdrom_state\": {"
            "\"seq\":%llu,\"has_disc\":%d,"
            "\"index\":%u,\"stat\":\"0x%02X\","
            "\"request\":\"0x%02X\","
            "\"irq_enable\":\"0x%02X\",\"irq_flag\":\"0x%02X\","
            "\"mode\":\"0x%02X\","
            "\"param_count\":%d,\"response_read\":%d,\"response_count\":%d,"
            "\"sector_available\":%d,\"sector_read_pos\":%d,\"sector_size\":%d,"
            "\"reading\":%d,\"read_msf\":[%d,%d,%d],"
            "\"read_cmd\":\"0x%02X\",\"read_delay\":%d,"
            "\"seek_msf\":[%u,%u,%u],"
            "\"pending\":{\"cmd\":\"0x%02X\",\"active\":%d,\"delay\":%d,\"phase\":%d},"
            "\"pending_time_continues\":%d,"
            "\"apv2\":{"
                "\"stock_mode\":%d,\"probe_active\":%d,"
                "\"total\":%u,\"success\":%u,"
                "\"probe_play_acks\":%llu,"
                "\"test04_count\":%llu,\"test05_count\":%llu,"
                "\"bypass_enabled\":%d,\"bypass_applied\":%d,"
                "\"bypass_frame\":%u,"
                "\"signature\":[\"0x%08X\",\"0x%08X\",\"0x%08X\"],"
                "\"state\":%u,\"timer\":%u,"
                "\"response\":[%u,%u,%u,%u,%u,%u,%u,%u]},"
            "\"queued\":{\"cmd\":\"0x%02X\",\"active\":%u,\"param_count\":%u},"
            "\"last_sector\":{\"lba\":%d,\"size\":%d,\"frame\":%u,"
            "\"mode\":\"0x%02X\",\"have_raw\":%u},"
            "\"int1_pended\":%llu,\"int1_lost\":%llu,\"int1_pending_now\":%u,"
            "\"i_stat\":\"0x%08X\"},\n",
            (unsigned long long)s.seq, s.has_disc,
            s.index_reg, s.stat_reg, s.request_reg,
            s.irq_enable, s.irq_flag, s.mode_reg,
            s.param_count, s.response_read, s.response_count,
            s.sector_available, s.sector_read_pos, s.sector_size,
            s.reading, s.read_min, s.read_sec, s.read_sect,
            s.read_cmd, s.read_delay,
            s.seek_min, s.seek_sec, s.seek_sect,
            s.pending_cmd, s.pending_pending, s.pending_delay, s.pending_phase,
            s.pending_time_continues,
            s.apv2_stock_mode, s.apv2_probe_active,
            s.apv2_total, s.apv2_success,
            (unsigned long long)s.apv2_probe_play_acks,
            (unsigned long long)s.apv2_test04_count,
            (unsigned long long)s.apv2_test05_count,
            s.fm_apv2_bypass_enabled, s.fm_apv2_bypass_applied,
            s.fm_apv2_bypass_frame,
            s.fm_apv2_bypass_seen_184, s.fm_apv2_bypass_seen_188,
            s.fm_apv2_bypass_seen_18c, s.fm_ap_state, s.fm_ap_timer,
            s.fm_ap_response[0], s.fm_ap_response[1],
            s.fm_ap_response[2], s.fm_ap_response[3],
            s.fm_ap_response[4], s.fm_ap_response[5],
            s.fm_ap_response[6], s.fm_ap_response[7],
            s.queued_cmd, s.queued_pending, s.queued_param_count,
            s.last_sector_lba, s.last_sector_size, s.last_sector_frame,
            s.last_sector_mode, s.last_sector_have_raw,
            (unsigned long long)s.int1_pended,
            (unsigned long long)s.int1_lost,
            s.int1_pending_now, s.i_stat);
    }

    {
        const CDROMCommandHistoryEntry *entries = NULL;
        uint64_t total = cdrom_debug_get_command_history(&entries);
        int count = (int)(total < 64u ? total : 64u);
        uint64_t start = total - (uint64_t)count;
        append_fmt(buf, sizeof(buf), &pos,
            "  \"cdrom_command_tail\": {\"total\":%llu,\"count\":%d,\"entries\":[",
            (unsigned long long)total, count);
        int emitted = 0;
        for (uint64_t seq = start; seq < total; ++seq) {
            const CDROMCommandHistoryEntry *e =
                &entries[seq % CDROM_COMMAND_HISTORY_CAP];
            if (e->seq != seq) continue;
            append_fmt(buf, sizeof(buf), &pos,
                "%s{\"seq\":%llu,\"frame\":%u,\"kind\":\"%c\","
                "\"cmd\":\"0x%02X\",\"params\":[",
                emitted ? "," : "", (unsigned long long)e->seq,
                e->frame, e->kind ? e->kind : '?', e->cmd);
            for (uint8_t i = 0; i < e->param_count && i < 16; ++i)
                append_fmt(buf, sizeof(buf), &pos, "%s\"0x%02X\"", i ? "," : "", e->params[i]);
            append_fmt(buf, sizeof(buf), &pos,
                "],\"stat\":\"0x%02X\",\"irq_enable\":\"0x%02X\","
                "\"irq_flag\":\"0x%02X\",\"mode\":\"0x%02X\","
                "\"seek_msf\":[%u,%u,%u],\"read_msf\":[%u,%u,%u],"
                "\"read_cmd\":\"0x%02X\",\"reading\":%u,"
                "\"pending_cmd\":\"0x%02X\",\"pending\":%u,"
                "\"queued_cmd\":\"0x%02X\",\"queued\":%u,"
                "\"func\":\"0x%08X\",\"pc\":\"0x%08X\"}",
                e->stat, e->irq_enable, e->irq_flag, e->mode,
                e->seek_min, e->seek_sec, e->seek_sect,
                e->read_min, e->read_sec, e->read_sect,
                e->read_cmd, e->reading,
                e->pending_cmd, e->pending_pending,
                e->queued_cmd, e->queued_pending,
                e->func, e->pc);
            emitted++;
        }
        append_str(buf, sizeof(buf), &pos, "]},\n");
    }

    {
        const CDROMTraceEntry *entries = NULL;
        uint64_t total = cdrom_debug_get_trace(&entries);
        int count = (int)(total < 256u ? total : 256u);
        uint64_t start = total - (uint64_t)count;
        append_fmt(buf, sizeof(buf), &pos,
            "  \"cdrom_trace_tail\": {\"total\":%llu,\"count\":%d,\"entries\":[",
            (unsigned long long)total, count);
        int emitted = 0;
        for (uint64_t seq = start; seq < total; ++seq) {
            const CDROMTraceEntry *e = &entries[seq % CDROM_TRACE_CAP];
            if (e->seq != seq) continue;
            append_fmt(buf, sizeof(buf), &pos,
                "%s{\"seq\":%llu,\"kind\":\"%c\",\"addr\":\"0x%08X\","
                "\"val\":\"0x%08X\",\"pc\":\"0x%08X\",\"func\":\"0x%08X\","
                "\"frame\":%u,\"index\":%u,\"stat\":\"0x%02X\","
                "\"irq_enable\":\"0x%02X\",\"irq_flag\":\"0x%02X\","
                "\"resp\":[%u,%u],\"pending\":[\"0x%02X\",%u,%d],"
                "\"reading\":%u,\"read_cmd\":\"0x%02X\",\"read_delay\":%d}",
                emitted ? "," : "", (unsigned long long)e->seq,
                e->kind ? e->kind : '?', e->addr, e->val,
                e->pc, e->func, e->frame,
                e->index_reg, e->stat_reg, e->irq_enable, e->irq_flag,
                e->response_read, e->response_count,
                e->pending_cmd, e->pending_pending, e->pending_delay,
                e->reading, e->read_cmd, e->read_delay);
            emitted++;
        }
        append_str(buf, sizeof(buf), &pos, "]},\n");
    }

    {
        const CDROMSectorHistoryEntry *entries = NULL;
        uint64_t total = cdrom_debug_get_sector_history(&entries);
        int count = (int)(total < 32u ? total : 32u);
        uint64_t start = total - (uint64_t)count;
        append_fmt(buf, sizeof(buf), &pos,
            "  \"cdrom_sector_tail\": {\"total\":%llu,\"count\":%d,\"entries\":[",
            (unsigned long long)total, count);
        int emitted = 0;
        for (uint64_t seq = start; seq < total; ++seq) {
            const CDROMSectorHistoryEntry *e =
                &entries[seq % CDROM_SECTOR_HISTORY_CAP];
            if (e->seq != seq) continue;
            append_fmt(buf, sizeof(buf), &pos,
                "%s{\"seq\":%llu,\"lba\":%d,\"size\":%d,\"frame\":%u,"
                "\"mode\":\"0x%02X\",\"raw_mode\":\"0x%02X\","
                "\"data\":%u,\"xa\":%u,\"skip\":%u}",
                emitted ? "," : "", (unsigned long long)e->seq,
                e->lba, e->size, e->frame, e->mode, e->raw_mode,
                e->data_delivered, e->xa_audio_delivered, e->skip_reason);
            emitted++;
        }
        append_str(buf, sizeof(buf), &pos, "]},\n");
    }

    /* Recursion fingerprint (build-independent GUEST addresses): the func entered
     * when the native stack guard tripped, plus the recent recompiled-function-
     * entry ring — for a runaway, recent_fn's tail repeats the recursing func, so
     * the report names the culprit even when the native stack_scan is absent (the
     * graceful-halt path) or unmappable (host offsets vs a different build). */
    {
        extern uint32_t g_psx_recent_fn[];
        extern uint32_t g_psx_recent_fn_i;
        extern uint32_t g_psx_recursion_func;
        enum { RECENT_FN_CAP = 64 };
        uint32_t total = g_psx_recent_fn_i;
        int count = (total < (uint32_t)RECENT_FN_CAP) ? (int)total : RECENT_FN_CAP;
        append_fmt(buf, sizeof(buf), &pos,
            "  \"recursion_func\": \"0x%08X\",\n"
            "  \"recent_fn\": {\n    \"total\": %u,\n    \"count\": %d,\n    \"addrs\": [",
            g_psx_recursion_func, total, count);
        uint32_t start = total - (uint32_t)count;
        for (int i = 0; i < count; i++) {
            uint32_t a = g_psx_recent_fn[(start + (uint32_t)i) & (RECENT_FN_CAP - 1u)];
            append_fmt(buf, sizeof(buf), &pos, "%s\"0x%08X\"", i == 0 ? "" : ",", a);
        }
        append_str(buf, sizeof(buf), &pos, "]\n  },\n");
    }

    /* Re-entry flight recorder (RECURSION_BUG.md §15): per-frame count of the
     * interp->0x8001A954 edge + cycle counter, leading up to the trip — answers
     * "ordinary bounded per-frame behavior that stopped terminating, or new edge?" */
    {
        extern int dirty_ram_re954_json(char *out, int cap);
        static char re954[8192];
        int k = dirty_ram_re954_json(re954, (int)sizeof(re954));
        if (k > 0) append_str(buf, sizeof(buf), &pos, re954);
    }

    /* Host-stack-usage profile (RECURSION_BUG.md §17): the climb curve —
     * flat-then-cliff vs linear-across-frames — that discriminates the freeze's
     * two models. Emitted BEFORE the early-flush below so it survives even if the
     * native_stack walk faults on the exhausted fiber stack. */
    {
        extern int stack_profile_json(char *out, int cap);
        static char sp[24576];
        int k = stack_profile_json(sp, (int)sizeof(sp));
        if (k > 0) {
            append_str(buf, sizeof(buf), &pos, "  \"stack_profile\": ");
            append_str(buf, sizeof(buf), &pos, sp);
            append_str(buf, sizeof(buf), &pos, ",\n");
        }
    }

    /* Boundary control-flow flight recorder (RECURSION_BUG.md §18): the per-frame
     * summary (shape over frames) + per-crossing detail (onset transfer). Emitted
     * before the early-flush so it survives a native_stack-walk fault. */
    {
        extern int dirty_ram_xprobe_json(char *out, int cap);
        static char xp[1048576];
        int k = dirty_ram_xprobe_json(xp, (int)sizeof(xp));
        if (k > 0) {
            append_str(buf, sizeof(buf), &pos, "  \"xprobe\": ");
            append_str(buf, sizeof(buf), &pos, xp);
            append_str(buf, sizeof(buf), &pos, ",\n");
        }
    }

    /* §19 compiled-entry depth profile: true intra-frame stack depth + raw TEB
     * values at the trip (real recursion vs garbage guard-read). */
    {
        extern int ce_profile_json(char *out, int cap);
        static char ce[49152];
        int k = ce_profile_json(ce, (int)sizeof(ce));
        if (k > 0) {
            append_str(buf, sizeof(buf), &pos, "  \"ce_profile\": ");
            append_str(buf, sizeof(buf), &pos, ce);
            append_str(buf, sizeof(buf), &pos, ",\n");
        }
    }

    /* Native call-stack snapshot — the TRUE recursion cycle (recent_fn above is
     * time-ordered and shows leaf churn, not the recursing frames).
     *
     * SAFETY: append_native_stack walks raw host stack memory and, despite its
     * VirtualQuery bounding, can still fault on a hostile crash state (e.g. the
     * recursion's own exhausted fiber stack). Because it runs INSIDE the dump,
     * such a fault would take the whole report with it (silent SIGSEGV, no
     * psx_last_run_report.json). So flush a complete-but-native_stack-less report
     * to disk FIRST; if the walk faults, the trigger context (reason, cpu,
     * recent_fn) is already on disk. If it survives, the final fwrite at the end
     * of this function overwrites this snapshot with the full report. */
#ifdef _WIN32
    {
        size_t safe_pos = pos;
        append_str(buf, sizeof(buf), &safe_pos, "  \"native_stack\": null\n}\n");
        FILE *pf = fopen(kReportPath, "wb");
        if (pf) { fwrite(buf, 1, safe_pos, pf); fclose(pf); }

        uintptr_t sp = 0;
        if (seh_info)
            sp = (uintptr_t)((EXCEPTION_POINTERS *)seh_info)->ContextRecord->Rsp;
        append_native_stack(buf, sizeof(buf), &pos, sp);
    }
#endif

    /* dispatch_ring tail (last 256) */
    {
        uint64_t total = crash_trace_dispatch_seq_get();
        int avail = (total < DISPATCH_TRACE_CAP) ? (int)total : DISPATCH_TRACE_CAP;
        int count = avail < 256 ? avail : 256;
        append_fmt(buf, sizeof(buf), &pos,
            "  \"dispatch_tail\": {\n"
            "    \"total\": %llu,\n"
            "    \"count\": %d,\n"
            "    \"addrs\": [",
            (unsigned long long)total, count);
        uint64_t start = total - (uint64_t)count;
        for (int i = 0; i < count; i++) {
            uint32_t a = crash_trace_dispatch_ring_get((int)((start + i) & (DISPATCH_TRACE_CAP - 1)));
            append_fmt(buf, sizeof(buf), &pos, "%s\"0x%08X\"", i == 0 ? "" : ",", a);
        }
        append_str(buf, sizeof(buf), &pos, "]\n  },\n");
    }

    /* unknown_dispatch tail (last 50) */
    {
        uint64_t total = crash_trace_unknown_seq_get();
        int avail = (total < UNKNOWN_DISPATCH_CAP) ? (int)total : UNKNOWN_DISPATCH_CAP;
        int count = avail < 50 ? avail : 50;
        append_fmt(buf, sizeof(buf), &pos,
            "  \"unknown_dispatch_tail\": {\n"
            "    \"total\": %llu,\n"
            "    \"count\": %d,\n"
            "    \"entries\": [",
            (unsigned long long)total, count);
        uint64_t start = total - (uint64_t)count;
        for (int i = 0; i < count; i++) {
            UnknownDispatchEntry e = crash_trace_unknown_get(start + i);
            append_fmt(buf, sizeof(buf), &pos,
                "%s{\"seq\":%llu,\"addr\":\"0x%08X\",\"phys\":\"0x%08X\","
                "\"ra\":\"0x%08X\",\"a0\":\"0x%08X\",\"a1\":\"0x%08X\","
                "\"frame\":%u}",
                i == 0 ? "" : ",",
                (unsigned long long)e.seq, e.addr, e.phys,
                e.ra, e.a0, e.a1, e.frame);
        }
        append_str(buf, sizeof(buf), &pos, "]\n  },\n");
    }

    /* dirty_block_log tail (last 100) */
    {
        uint64_t total = g_dirty_ram_block_log_seq;
        uint64_t avail = (total < DIRTY_RAM_BLOCK_LOG_CAP) ? total : DIRTY_RAM_BLOCK_LOG_CAP;
        int count = (int)((avail < 100) ? avail : 100);
        append_fmt(buf, sizeof(buf), &pos,
            "  \"dirty_block_tail\": {\n"
            "    \"total\": %llu,\n"
            "    \"count\": %d,\n"
            "    \"entries\": [",
            (unsigned long long)total, count);
        uint64_t start = total - (uint64_t)count;
        for (int i = 0; i < count; i++) {
            DirtyRamBlockLogEntry *e =
                &g_dirty_ram_block_log[(start + i) & (DIRTY_RAM_BLOCK_LOG_CAP - 1u)];
            append_fmt(buf, sizeof(buf), &pos,
                "%s{\"seq\":%llu,\"target\":\"0x%08X\",\"ra\":\"0x%08X\","
                "\"a0\":\"0x%08X\",\"a1\":\"0x%08X\",\"frame\":%u}",
                i == 0 ? "" : ",",
                (unsigned long long)e->seq,
                e->target, e->ra, e->a0, e->a1, e->frame);
        }
        append_str(buf, sizeof(buf), &pos, "]\n  }\n");
    }

    append_str(buf, sizeof(buf), &pos, "}\n");

    /* Write to file. Overwrite previous report. */
    FILE *f = fopen(kReportPath, "wb");
    if (f) {
        fwrite(buf, 1, pos, f);
        fclose(f);
    }
}

/* ── Fatal halt ──────────────────────────────────────────────────────── */

const char *g_psx_fatal_reason = NULL;

/* freeze_heartbeat.c — full ring dump with wedge_kind "fatal". */
extern void freeze_heartbeat_fatal_dump(const char *reason);

void psx_fatal_halt(const char *reason) {
    /* Re-entry guard: a post-mortem TCP command served from the halt
     * loop below can itself trip a fatal site. Don't re-dump (the first
     * fatal is the real one) and don't recurse another serve loop. */
    static int s_halted = 0;
    if (!s_halted) {
        s_halted = 1;
        g_psx_fatal_reason = reason ? reason : "(fatal)";
        psx_crash_trace_dump(g_psx_fatal_reason, NULL);
        freeze_heartbeat_fatal_dump(g_psx_fatal_reason);
    }
#ifndef PSX_NO_DEBUG_TOOLS
    /* Halt-and-serve: emulation is dead but the rings are not. Keep the
     * TCP debug server pumping on this (main) thread so a post-mortem
     * client can run wtrace_dump / read_ram / screenshot / etc. against
     * the exact crash state. */
    extern void debug_server_poll(void);
    fprintf(stderr,
            "FATAL: %s — emulation halted; TCP debug server stays live "
            "for post-mortem ring queries.\n", g_psx_fatal_reason);
    fflush(stderr);
    for (;;) {
        debug_server_poll();
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec req = {0, 1000000};
        nanosleep(&req, NULL);
#endif
    }
#else
    exit(1);
#endif
}

/* ── Crash handlers ──────────────────────────────────────────────────── */

#include <signal.h>

static void psx_signal_handler(int sig) {
    static char reason[64];
    snprintf(reason, sizeof(reason), "signal_%d", sig);
    psx_crash_trace_dump(reason, NULL);
    /* Involuntary death: dump the full freeze-style rings too, so the
     * crash doesn't take every ring with it. freeze_heartbeat_fatal_dump
     * guards against overwriting an earlier fatal dump. */
    if (!g_psx_fatal_reason) g_psx_fatal_reason = reason;
    freeze_heartbeat_fatal_dump(reason);
    /* Reraise default handler so debugger / OS can also act. */
    signal(sig, SIG_DFL);
    raise(sig);
}

#ifdef _WIN32
static LONG WINAPI psx_seh_handler(EXCEPTION_POINTERS *info) {
    psx_crash_trace_dump("seh", info);
    /* Same as the signal path: keep the rings on involuntary death. */
    if (!g_psx_fatal_reason) g_psx_fatal_reason = "seh";
    freeze_heartbeat_fatal_dump("seh");
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void psx_atexit_handler(void) {
    /* Only dump if no crash dump has been written yet. We can't easily
     * detect that, so just always overwrite — last write wins. */
    psx_crash_trace_dump("atexit", NULL);
}

void psx_crash_trace_install_handlers(void) {
    signal(SIGSEGV, psx_signal_handler);
    signal(SIGABRT, psx_signal_handler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(psx_seh_handler);
    /* Suppress Windows error dialog so SEH unwinds straight to our
     * filter and we can write the report without the user having to
     * dismiss a popup first. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    atexit(psx_atexit_handler);
}
