#include "controls.h"
#include "../params.h"

#define NO_DSP 255u
#define ID_PRESET 254u
enum { I_PRESET, I_MIX, I_CLOCK, I_VOLUME, I_TIME, I_SMEAR, I_CHANCE, I_GLITCH, I_RATE, I_DEPTH, I_SHAPE, I_MOD,
       I_DISSOLVE, I_FILTER, I_LMODE, I_LENGTH, I_MOTION, I_LOOPLV, I_ROUTE, I_N };

static const struct { const char *name; uint8_t sec, id; uint16_t n; } item[I_N] = {
    { "PRESET",   SEC_MAIN,  ID_PRESET,   DC_SLOTS },
    { "MIX",      SEC_MAIN,  DC_MIX,      256 },
    { "CLOCK",    SEC_MAIN,  DC_CLOCK,    DC_CLOCKS },
    { "VOLUME",   SEC_MAIN,  NO_DSP,      256 },
    { "TIME",     SEC_WET,   DC_TIME,     256 },
    { "SMEAR",    SEC_WET,   DC_SMEAR,    256 },
    { "CHANCE",   SEC_WET,   DC_CHANCE,   256 },
    { "GLITCH",   SEC_WET,   DC_GLITCH,   256 },
    { "RATE",     SEC_MOD,   DC_RATE,     256 },
    { "DEPTH",    SEC_MOD,   DC_DEPTH,    256 },
    { "SHAPE",    SEC_MOD,   DC_SHAPE,    DC_SHAPES },
    { "MOD",      SEC_MOD,   DC_MODTYPE,  2 },
    { "DISSOLVE", SEC_COLOR, DC_DISSOLVE, 256 },
    { "FILTER",   SEC_COLOR, DC_FILTER,   256 },
    { "MODE",     SEC_LOOP,  DC_LMODE,    2 },
    { "LENGTH",   SEC_LOOP,  DC_LENGTH,   256 },
    { "SPEED",    SEC_LOOP,  DC_MOTION,   256 },
    { "LOOP LV",  SEC_LOOP,  DC_LOOPLV,   256 },
    { "ROUTE",    SEC_LOOP,  DC_ROUTE,    3 },
};
static const char *const sec_name[SEC_N] = { "MAIN", "WET", "MOD", "COLOR", "LOOP" };

static uint32_t val[DC_NPARAM];
static unsigned volume, sel, live, have_knob, knob_ref, last_knob;
static uint32_t dirty;                       /* bit = DSP parameter id */

/* presets: what is stored (dc_persist_*), version 1 */
#define SNAP (DC_NPARAM + 1u)                /* every setting's value, then VOLUME */
static struct { uint8_t version, last, used, spare; uint8_t slot[DC_SLOTS][SNAP]; } st;
static unsigned preset, hl;                  /* the loaded slot; the slot PRESET highlights */
static unsigned saved_ticks, save_err, loaded;   /* the message after a click: SAVED / LOADED, for saved_ticks */
#define HOLD 300u                            /* 3 s: holding PARA turns the effect on / off */
#define HOLD_SHOW 100u                       /* the countdown shows after 1 s */
#define TAP_MAX 80u                          /* a press up to 0.8 s is a tap */
#define DEBOUNCE 3u                          /* 30 ms */

/* A switch from the platform's raw edges (not debounced). A press acts at once unless it comes within
 * DEBOUNCE of a release (a bounce); a release counts once the switch has stayed open for DEBOUNCE. */
typedef struct { uint8_t raw, down; uint16_t held, quiet, since_up; } btn_t;
static btn_t para, foot;

static void set(unsigned id, uint32_t v)
{
    if (val[id] != v) { val[id] = v; dirty |= 1u << id; }
}

void dc_ui_init(void)
{
    static const uint32_t def[DC_NPARAM] = DC_DEFAULTS;
    unsigned i;
    for (i = 0; i < DC_NPARAM; i++) val[i] = def[i];
    volume = 255; sel = I_PRESET; live = have_knob = knob_ref = last_knob = 0; dirty = 0;
    for (i = 0; i < sizeof st; i++) ((uint8_t *)&st)[i] = 0;
    st.version = 1; preset = hl = 0; saved_ticks = save_err = 0;
    for (i = 0; i < sizeof para; i++) { ((uint8_t *)&para)[i] = 0; ((uint8_t *)&foot)[i] = 0; }
    para.since_up = foot.since_up = DEBOUNCE;
}

static void snapshot(uint8_t *s)
{
    unsigned i;
    for (i = 0; i < DC_NPARAM; i++) s[i] = (uint8_t)val[i];
    s[DC_NPARAM] = (uint8_t)volume;
}

static void apply(const uint8_t *s)          /* everything but on / off and the caught loop */
{
    static const uint8_t max[DC_NPARAM] = { 1, 255, DC_CLOCKS - 1, 1, 1, 255, 255, 255, 2, 255, 255, 255, 255, 255,
                                            DC_SHAPES - 1, 1, 255, 255, 255 };
    unsigned i;
    for (i = 0; i < DC_NPARAM; i++)
        if (i != DC_ON && i != DC_CATCH) set(i, s[i] > max[i] ? max[i] : s[i]);
    volume = s[DC_NPARAM];
}

static int changed(void)
{
    uint8_t now[SNAP];
    unsigned i;
    if (!(st.used & (1u << preset))) return 1;
    snapshot(now);
    for (i = 0; i < SNAP; i++) if (i != DC_ON && i != DC_CATCH && now[i] != st.slot[preset][i]) return 1;
    return 0;
}

void dc_ui_restore(void)
{
    uint8_t b[sizeof st];
    unsigned i;
    if (dc_persist_load(b, sizeof b) != sizeof st || b[0] != 1) return;     /* nothing (valid) stored yet */
    for (i = 0; i < sizeof st; i++) ((uint8_t *)&st)[i] = b[i];
    preset = hl = st.last < DC_SLOTS ? st.last : 0;
    if (st.used & (1u << preset)) apply(st.slot[preset]);
}

static void load_preset(unsigned p)          /* PARA tap on PRESET: the highlighted slot, if it holds a sound */
{
    if (!(st.used & (1u << p))) return;
    preset = p;
    apply(st.slot[p]);
    if (st.last != p) { st.last = (uint8_t)p; dc_persist_save(&st, sizeof st); }   /* the slot to start on */
    saved_ticks = 150; loaded = 1; save_err = 0;
}

static void save_preset(unsigned p)
{
    preset = hl = p;
    snapshot(st.slot[preset]);
    st.used |= (uint8_t)(1u << preset);
    st.last = (uint8_t)preset;
    save_err = dc_persist_save(&st, sizeof st) != 0;
    saved_ticks = 150; loaded = 0;
}

void dc_ui_resend_all(void) { dirty = (1u << DC_NPARAM) - 1u; }

void dc_ui_turn(int detents)
{
    int n;
    if (!detents) return;
    n = ((int)sel + detents) % (int)I_N;
    sel = (unsigned)(n < 0 ? n + (int)I_N : n);
    live = 0; have_knob = 0;                 /* the new setting keeps its value until the knob moves */
    saved_ticks = 0;                         /* moving on clears "SAVED" */
    hl = preset;                             /* PRESET opens on the loaded slot */
}

/* foot: the looper only, at once: catch / let go */
static void foot_press(void) { set(DC_CATCH, !val[DC_CATCH]); }

/* PARA held 3 s: the effect on / off. The looper is left as it is (while off, the output is the dry signal). */
static void para_hold(void) { set(DC_ON, !val[DC_ON]); }

/* PARA click: on the PRESET page with another slot highlighted, load it (an empty one: save the sound there);
 * otherwise save the sound into the active preset. */
static void para_release(unsigned held)
{
    if (held >= TAP_MAX) return;
    if (sel == I_PRESET && hl != preset) {
        if (st.used & (1u << hl)) load_preset(hl); else save_preset(hl);
    } else save_preset(preset);
}

static void btn_edge(btn_t *b, int down, void (*press)(void))
{
    b->raw = (uint8_t)down; b->quiet = 0;
    if (down && !b->down && b->since_up >= DEBOUNCE) { b->down = 1; b->held = 0; if (press) press(); }
}
static void btn_tick(btn_t *b, void (*press)(void), void (*hold)(void), void (*release)(unsigned))
{
    if (b->quiet < 0xFFFF) b->quiet++;
    if (b->down) {
        if (b->held < 0xFFFF && ++b->held == HOLD && hold) hold();
        if (!b->raw && b->quiet >= DEBOUNCE) { b->down = 0; b->since_up = 0; if (release) release(b->held); }
    } else {
        if (b->since_up < 0xFFFF) b->since_up++;
        if (b->raw && b->quiet >= DEBOUNCE) { b->down = 1; b->held = 0; if (press) press(); }   /* a late press */
    }
}

void dc_ui_foot_down(void) { btn_edge(&foot, 1, foot_press); }
void dc_ui_foot_up(void) { btn_edge(&foot, 0, 0); }
void dc_ui_para_down(void) { btn_edge(&para, 1, 0); }
void dc_ui_para_up(void) { btn_edge(&para, 0, 0); }

void dc_ui_tick(void)
{
    btn_tick(&para, 0, para_hold, para_release);
    btn_tick(&foot, foot_press, 0, 0);
    if (saved_ticks) saved_ticks--;
}

unsigned dc_ui_preset(void) { return preset; }
static unsigned para_counting(void) { return para.down && para.held >= HOLD_SHOW && para.held < HOLD; }
unsigned dc_ui_overlay(void) { return saved_ticks || para_counting(); }

#define KNOB_MOVE 6
void dc_ui_knob(unsigned knob)
{
    unsigned v, n = item[sel].n;
    int d;
    if (knob > 255u) knob = 255u;
    if (!have_knob) { have_knob = 1; knob_ref = last_knob = knob; return; }
    if (!live) {
        d = (int)knob - (int)knob_ref;
        if (d > -KNOB_MOVE && d < KNOB_MOVE) return;
        live = 1;
    } else if (knob != 0 && knob != 255 && knob + 1 >= last_knob && knob <= last_knob + 1) return;   /* ADC wobble */
    last_knob = knob;
    saved_ticks = 0;
    v = n == 256 ? knob : knob * n / 256u;
    if (item[sel].id == NO_DSP) volume = v;
    else if (item[sel].id == ID_PRESET) hl = v;      /* highlight only: PARA loads it */
    else set(item[sel].id, v);
}

int dc_ui_take(uint32_t *id, uint32_t *value)
{
    unsigned i;
    /* catch and on go last, so they act with every other setting already in place */
    for (i = 0; i < DC_NPARAM + 2u; i++) {
        unsigned k = i < DC_NPARAM ? i : (i == DC_NPARAM ? DC_CATCH : DC_ON);
        if (i < DC_NPARAM && (k == DC_CATCH || k == DC_ON)) continue;
        if (dirty & (1u << k)) { dirty &= ~(1u << k); *id = k; *value = val[k]; return 1; }
    }
    return 0;
}

unsigned dc_ui_level(void) { return volume; }
unsigned dc_ui_on(void) { return val[DC_ON]; }
unsigned dc_ui_caught(void) { return val[DC_CATCH]; }
unsigned dc_ui_live(void) { return live; }
unsigned dc_ui_sel(void) { return sel; }
unsigned dc_ui_count(void) { return I_N; }
unsigned dc_ui_section(unsigned i) { return i < I_N ? item[i].sec : 0; }
const char *dc_ui_section_name(unsigned s) { return s < SEC_N ? sec_name[s] : ""; }

static unsigned cur(void)
{
    return item[sel].id == NO_DSP ? volume : item[sel].id == ID_PRESET ? hl : val[item[sel].id];
}

const char *dc_ui_name(void)
{
    if (saved_ticks) return save_err ? "SAVE ERR" : loaded ? "LOADED" : "SAVED";
    if (para_counting()) return val[DC_ON] ? "OFF IN" : "ON IN";
    if (sel == I_MOTION) return val[DC_LMODE] ? "DRIFT" : "SPEED";
    if (sel == I_CHANCE && val[DC_SHAPE] == DC_SHAPES - 1u) return "SENSE";
    return item[sel].name;
}

unsigned dc_ui_frac(void)
{
    unsigned n = item[sel].n;
    if (saved_ticks) return 255;
    if (para_counting()) return (para.held - HOLD_SHOW) * 255u / (HOLD - HOLD_SHOW);
    return n == 256 ? cur() : cur() * 255u / (n - 1u);
}

/* ---- numbers as the DSP computes them (dream.h), in integers ---- */

/* base * 2^(x / 65536), within 0.3 % */
static uint32_t exp2q(uint32_t base, uint32_t x)
{
    uint32_t i = x >> 16, f = x & 0xFFFFu;
    uint32_t m = 65536u + ((f * (43024u + ((22512u * f) >> 16))) >> 16);
    return ((base << i) * m) >> 16;          /* callers keep base << i below 32768: no overflow */
}

static const uint8_t clk_num[DC_CLOCKS] = { 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1 };
static const uint8_t clk_den[DC_CLOCKS] = { 32, 64, 16, 32, 8, 16, 4, 8, 2, 4, 1 };
static const char *const clk_name[DC_CLOCKS] = { "1.4K", "2.1K", "2.8K", "4.1K", "5.5K", "8.3K", "11K", "17K",
                                                 "22K", "33K", "44K" };
static const char *const shape_name[DC_SHAPES] = { "SINE", "SQUARE", "RAMP", "SAW", "RND SIN", "RND SQR", "ENV" };
static const char *const tape_name[8] = { "REV 4X", "REV 2X", "REV 1X", "REV.5X", "0.5X", "1X", "2X", "4X" };
static const char *const route_name[3] = { "IN", "IN+LOOP", "LOOP" };

static char *str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *num(char *p, unsigned v)
{
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *pct(char *p, unsigned v, unsigned full) { p = num(p, (v * 100u + full / 2u) / full); *p++ = '%'; return p; }

static char *ms_at_clock(char *p, uint32_t samples)          /* samples at the CLOCK rate -> "120MS" / "2.4S" */
{
    unsigned c = val[DC_CLOCK];
    uint32_t ms = (samples * 1000u / clk_num[c]) * clk_den[c] / 44100u;
    if (ms < 1000u) { p = num(p, ms); return str(p, "MS"); }
    if (ms < 10000u) { p = num(p, ms / 1000u); *p++ = '.'; p = num(p, ms / 100u % 10u); *p++ = 'S'; return p; }
    p = num(p, ms / 1000u); *p++ = 'S'; return p;
}

static char *hz(char *p, uint32_t f)
{
    if (f < 1000u) return num(p, f);
    p = num(p, f / 1000u);
    if (f < 10000u) { *p++ = '.'; p = num(p, f / 100u % 10u); }
    *p++ = 'K'; return p;
}

unsigned dc_ui_rate_x100(void) { return exp2q(5u, val[DC_RATE] * 2032u); }

void dc_ui_value(char *v)
{
    char *p = v;
    unsigned x = cur();
    if (dc_ui_overlay()) {                   /* saving: the slot it goes to */
        if (saved_ticks) { *p++ = 'P'; *p++ = (char)('1' + preset); }
        else p = str(p, val[DC_ON] ? "BYPASS" : "ON");
        *p = 0;
        return;
    }
    switch (sel) {
    case I_PRESET:                           /* x = the highlighted slot */
        if (!(st.used & (1u << x))) { *p++ = (char)('1' + x); p = str(p, " EMPTY"); }
        else if (x != preset) { p = str(p, "LOAD P"); *p++ = (char)('1' + x); }
        else { *p++ = 'P'; *p++ = (char)('1' + x); if (changed()) *p++ = '*'; }
        break;
    case I_CLOCK: p = str(p, clk_name[x]); break;
    case I_TIME: {
        uint32_t steps = exp2q(200u, x * 1783u);
        if (steps > 24512u) steps = 24512u;
        p = ms_at_clock(p, steps); break;
    }
    case I_CHANCE:
        if (val[DC_SHAPE] == DC_SHAPES - 1u) p = pct(p, x, 255);
        else if (x < 128u) p = pct(p, x, 128);
        else if (x == 128u) p = str(p, "ALWAYS");
        else { p = str(p, "FB "); p = pct(p, x - 128u, 127); }
        break;
    case I_GLITCH:
        if (x < 43u) p = str(p, "HALF");
        else if (x < 86u) p = str(p, "NORMAL");
        else if (x < 128u) p = str(p, "DOUBLE");
        else { p = str(p, "RND "); p = pct(p, x - 128u, 127); }
        break;
    case I_RATE: {
        unsigned f = dc_ui_rate_x100();
        if (f < 100u) { p = str(p, "0."); *p++ = (char)('0' + f / 10u); *p++ = (char)('0' + f % 10u); }
        else if (f < 1000u) { p = num(p, f / 100u); *p++ = '.'; p = num(p, f / 10u % 10u); }
        else p = num(p, f / 100u);
        p = str(p, "HZ"); break;
    }
    case I_SHAPE: p = str(p, shape_name[x]); break;
    case I_MOD: p = str(p, x ? "VIBRATO" : "TREMOLO"); break;
    case I_DISSOLVE:
        if (x < 120u) { p = str(p, "LOFI "); p = num(p, ((120u - x) * 100u + 60u) / 120u); }
        else if (x <= 135u) p = str(p, "CLEAN");
        else {
            unsigned st = (x - 136u) / 30u;
            if (st > 3u) st = 3u;
            p = str(p, st ? "REV/" : "REV 1X");
            if (st) p = num(p, 1u << st);
        }
        break;
    case I_FILTER:
        if (x < 120u) { p = str(p, "LP "); p = hz(p, exp2q(150u, x * 3517u)); }
        else if (x <= 135u) p = str(p, "OPEN");
        else { p = str(p, "HP "); p = hz(p, exp2q(20u, (x - 136u) * 3839u)); }
        break;
    case I_LMODE: p = str(p, x ? "STRETCH" : "TAPE"); break;
    case I_LENGTH:
        p = ms_at_clock(p, val[DC_LMODE] ? exp2q(256u, x * 1285u) : exp2q(2012u, x * 1028u));
        break;
    case I_MOTION:
        if (!val[DC_LMODE]) p = str(p, tape_name[x >> 5]);
        else {
            int d = (int)x - 128;
            unsigned a = (unsigned)(d < 0 ? -d : d);
            if (a > 127u) a = 127u;
            if (a < 3u) p = str(p, "FROZEN");
            else { p = str(p, d < 0 ? "REV " : "FWD "); p = num(p, a * a * 100u / 16129u); *p++ = '%'; }
        }
        break;
    case I_ROUTE: p = str(p, route_name[x]); break;
    default: p = pct(p, x, 255); break;                       /* MIX, VOLUME, SMEAR, DEPTH, LOOP LV */
    }
    *p = 0;
}
