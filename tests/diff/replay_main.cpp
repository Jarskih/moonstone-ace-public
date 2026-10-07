// Host replay of recorded differential cases against lifted C++ routines.
//   replay <repo_root> <label> <binary> <cases.txt>
// Looks <binary>/<label> up in the generated registry (registry_table.inc, made
// by tools/diffharness/run_host.py from `extern "C" void lab_<LABEL>(Regs&,Mem&)`
// definitions under src/lifted/<binary>/).  Prints one summary line per label
// and, on failure, the first mismatch with its input case.  Exit 0 = all pass.
// No STL on purpose (the host clang here is older than the installed MSVC STL).
#include "ms/regs.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using namespace ms;

typedef void (*LiftFn)(Regs&, Mem&);
struct Entry { const char* binary; const char* label; LiftFn fn; };
#include "registry_table.inc"   // kRegistry[], kRegistryCount

namespace {

struct Region {
    uint32_t base = 0, size = 0;
    uint8_t* data = nullptr;
    uint8_t* dirty = nullptr;   // 1 = byte written by the routine
    void init(uint32_t b, uint32_t s, const uint8_t* src) {
        free(data); free(dirty);
        base = b; size = s;
        data = (uint8_t*)calloc(s ? s : 1, 1);
        dirty = (uint8_t*)calloc(s ? s : 1, 1);
        if (src) memcpy(data, src, s);
    }
};

// Buffer-backed big-endian memory; unmapped accesses are recorded as a fault.
struct HostMem : Mem {
    Region reg[3];
    char fault[96] = "";
    ~HostMem() { for (auto& r : reg) { free(r.data); free(r.dirty); } }
    Region* find(uint32_t a) {
        for (auto& r : reg) if (a >= r.base && a - r.base < r.size) return &r;
        return nullptr;
    }
    uint8_t r8(uint32_t a) override {
        Region* r = find(a);
        if (!r) { if (!fault[0]) snprintf(fault, sizeof fault, "read of unmapped $%08X", a); return 0; }
        return r->data[a - r->base];
    }
    void w8(uint32_t a, uint8_t v) override {
        Region* r = find(a);
        if (!r) { if (!fault[0]) snprintf(fault, sizeof fault, "write to unmapped $%08X", a); return; }
        r->data[a - r->base] = v; r->dirty[a - r->base] = 1;
    }
    bool wasWritten(uint32_t a) { Region* r = find(a); return r && r->dirty[a - r->base]; }
};

const int MAXRUN = 64;
struct Run { uint32_t addr; size_t len; uint8_t* data; };
struct Case {
    Regs in, out;
    Run mem[MAXRUN], writes[MAXRUN];
    int nmem = 0, nw = 0;
    char inLine[512];
    void clear() {
        for (int i = 0; i < nmem; i++) free(mem[i].data);
        for (int i = 0; i < nw; i++) free(writes[i].data);
        nmem = nw = 0; memset(&in, 0, sizeof in); memset(&out, 0, sizeof out); inLine[0] = 0;
    }
};

uint8_t* unhex(const char* s, size_t& n) {
    size_t cap = strlen(s) / 2 + 1; uint8_t* v = (uint8_t*)malloc(cap); n = 0;
    for (; s[0] && s[1] && s[0] != '\n' && s[0] != '\r'; s += 2) {
        char t[3] = {s[0], s[1], 0};
        v[n++] = (uint8_t)strtoul(t, nullptr, 16);
    }
    return v;
}

bool parseRegs(const char* p, Regs& r) {
    char* e;
    if (*p++ != 'd') return false;
    for (int i = 0; i < 8; i++) { r.d[i] = (uint32_t)strtoul(p, &e, 16); p = e; }
    while (*p == ' ') p++;
    if (*p++ != 'a') return false;
    for (int i = 0; i < 8; i++) { r.a[i] = (uint32_t)strtoul(p, &e, 16); p = e; }
    while (*p == ' ') p++;
    if (strncmp(p, "ccr", 3)) return false;
    r.sr = (uint16_t)strtoul(p + 3, &e, 16);
    return true;
}

void fmtCcr(char* o, unsigned c) {
    sprintf(o, "$%02X (XNZVC=%d%d%d%d%d)", c & 31, !!(c & 16), !!(c & 8), !!(c & 4), !!(c & 2), !!(c & 1));
}

// Returns true on pass; else fills msg.
bool check(LiftFn fn, Case& c, const uint8_t* image, uint32_t imageSize, const uint32_t* geo, char* msg) {
    HostMem m;
    m.reg[0].init(geo[0], imageSize, image);
    m.reg[1].init(geo[1], geo[2], nullptr);
    m.reg[2].init(geo[3], geo[4], nullptr);
    for (int i = 0; i < c.nmem; i++)
        for (size_t k = 0; k < c.mem[i].len; k++) {
            Region* r = m.find(c.mem[i].addr + (uint32_t)k);
            if (r) r->data[c.mem[i].addr + k - r->base] = c.mem[i].data[k];
        }
    Regs R = c.in; R.sr &= CCR_MASK;
    fn(R, m);
    msg[0] = 0;
    if (m.fault[0]) { snprintf(msg, 160, "memory fault: %s", m.fault); return false; }
    for (int i = 0; i < 8; i++)
        if (R.d[i] != c.out.d[i]) { snprintf(msg, 160, "D%d expected $%08X got $%08X", i, c.out.d[i], R.d[i]); return false; }
    for (int i = 0; i < 8; i++)
        if (R.a[i] != c.out.a[i]) { snprintf(msg, 160, "A%d expected $%08X got $%08X", i, c.out.a[i], R.a[i]); return false; }
    if ((R.sr & CCR_MASK) != (c.out.sr & CCR_MASK)) {
        char e[40], g[40]; fmtCcr(e, c.out.sr); fmtCcr(g, R.sr);
        snprintf(msg, 160, "CCR expected %s got %s", e, g); return false;
    }
    // Dirty memory: identical written-address set and identical final bytes.
    size_t expected = 0;
    for (int i = 0; i < c.nw; i++)
        for (size_t k = 0; k < c.writes[i].len; k++) {
            uint32_t a = c.writes[i].addr + (uint32_t)k; expected++;
            if (!m.wasWritten(a)) { snprintf(msg, 160, "memory $%08X expected to be written", a); return false; }
            if (m.r8(a) != c.writes[i].data[k]) {
                snprintf(msg, 160, "memory $%08X expected $%02X got $%02X", a, c.writes[i].data[k], m.r8(a)); return false;
            }
        }
    size_t actual = 0;
    for (auto& r : m.reg) for (uint32_t k = 0; k < r.size; k++) actual += r.dirty[k];
    if (actual != expected) { snprintf(msg, 160, "routine wrote %zu byte(s), original wrote %zu", actual, expected); return false; }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: replay root label binary cases.txt\n"); return 2; }
    const char* root = argv[1]; const char* label = argv[2]; const char* binary = argv[3];
    LiftFn fn = nullptr;
    for (unsigned i = 0; i < kRegistryCount; i++)
        if (!strcmp(binary, kRegistry[i].binary) && !strcmp(label, kRegistry[i].label)) fn = kRegistry[i].fn;
    if (!fn) { printf("FAIL %s/%s: not in registry\n", binary, label); return 1; }

    FILE* f = fopen(argv[4], "r");
    if (!f) { perror("cases"); return 2; }
    uint8_t* image = nullptr; uint32_t imageSize = 0;
    uint32_t geo[5] = {0, 0, 0, 0, 0};   // imageBase, stackBase, stackSize, scratchBase, scratchSize
    static Case cur; cur.clear();
    static char line[1 << 17];
    unsigned pass = 0, fail = 0, total = 0; int caseNo = -1;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "image ", 6)) {
            char path[512]; unsigned base, size;
            sscanf(line + 6, "%511s %x %x", path, &base, &size);
            char full[1100]; snprintf(full, sizeof full, "%s/%s", root, path);
            FILE* g = fopen(full, "rb");
            if (!g) { printf("FAIL %s/%s: cannot open image %s (run the generator)\n", binary, label, full); return 2; }
            image = (uint8_t*)malloc(size); imageSize = size;
            if (fread(image, 1, size, g) != size) { printf("FAIL: short image\n"); return 2; }
            fclose(g); geo[0] = base;
        } else if (!strncmp(line, "stack ", 6)) sscanf(line + 6, "%x %x", &geo[1], &geo[2]);
        else if (!strncmp(line, "scratch ", 8)) sscanf(line + 8, "%x %x", &geo[3], &geo[4]);
        else if (!strncmp(line, "case ", 5)) { cur.clear(); caseNo = atoi(line + 5); }
        else if (!strncmp(line, "in ", 3)) { parseRegs(line + 3, cur.in); snprintf(cur.inLine, sizeof cur.inLine, "%s", line + 3); }
        else if (!strncmp(line, "mem ", 4) || !strncmp(line, "w ", 2)) {
            bool isMem = line[0] == 'm'; char* e;
            uint32_t a = (uint32_t)strtoul(line + (isMem ? 4 : 2), &e, 16);
            Run* r = isMem ? &cur.mem[cur.nmem++] : &cur.writes[cur.nw++];
            r->addr = a; r->data = unhex(e + 1, r->len);
        } else if (!strncmp(line, "out ", 4)) parseRegs(line + 4, cur.out);
        else if (!strncmp(line, "end", 3)) {
            char msg[160]; total++;
            if (check(fn, cur, image, imageSize, geo, msg)) pass++;
            else { if (!fail) printf("FAIL %s/%s case %d: %s\n  input: %s", binary, label, caseNo, msg, cur.inLine); fail++; }
        }
    }
    fclose(f);
    printf("%s %s/%s: %u/%u cases passed\n", fail ? "FAIL" : "PASS", binary, label, pass, total);
    return fail ? 1 : 0;
}
