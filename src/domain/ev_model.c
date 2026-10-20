/* ev_model.c - scenario loading with strict validation. */
#include "ev_model.h"
#include "ev_mem.h"
#include "ev_sort.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ev_config_defaults(ev_config *c) {
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "unnamed");
    c->budget = 500;
    c->battery_kwh = 30.0;
    c->cons_kwh_km = 0.18;
    c->reserve_frac = 0.10;
    c->target_frac = 0.80;
    c->regen_eff = 0.60;
    c->elev_kwh_per_m = 0.0045; /* ~1.6 t vehicle: m*g*h/3.6e6 /drivetrain efficiency */
    c->speed_kmh = 40.0;
    c->max_detour_km = 15.0;
    c->grid_kw = 400.0;
    c->window_min = 120;
    c->session_min = 30;
    c->horizon_h = 24;
}

double ev_arc_energy(const ev_config *c, double km, double dz_m) {
    double e = km * c->cons_kwh_km;
    if (dz_m > 0) e += dz_m * c->elev_kwh_per_m;
    else e += dz_m * c->elev_kwh_per_m * c->regen_eff; /* regenerative braking: may go negative */
    return e;
}

/* ------------------------------------------------------------ error helper */
static void seterr(char *err, size_t cap, const char *fmt, ...) {
    if (!err || !cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------ file reading */
static char *read_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)ev_malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (len) *len = (long)got;
    return buf;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

/* Split line into fields on ','; returns count (max maxf). */
static int split(char *line, char **fields, int maxf) {
    int n = 0;
    char *p = line;
    for (;;) {
        char *c = strchr(p, ',');
        if (n < maxf) fields[n] = p;
        n++;
        if (!c) break;
        *c = '\0';
        p = c + 1;
    }
    for (int i = 0; i < n && i < maxf; i++) fields[i] = trim(fields[i]);
    return n;
}

static int parse_int(const char *s, long *out) {
    if (!*s) return 0;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *end) return 0;
    *out = v;
    return 1;
}

static int parse_dbl(const char *s, double *out) {
    if (!*s) return 0;
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || *end || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

typedef struct {
    char *buf;
    char *cur;
    int lineno;
    const char *path;
} csv_reader;

static int csv_open_text(csv_reader *r, const char *text, const char *path) {
    r->buf = NULL; r->cur = NULL; r->lineno = 0; r->path = path;
    if (!text) return 0;
    size_t n = strlen(text);
    r->buf = (char *)ev_malloc(n + 1);
    memcpy(r->buf, text, n + 1);
    r->cur = r->buf;
    return 1;
}

/* next non-empty, non-comment line or NULL */
static char *csv_next(csv_reader *r) {
    while (r->cur && *r->cur) {
        char *line = r->cur;
        char *nl = strchr(line, '\n');
        if (nl) { *nl = '\0'; r->cur = nl + 1; } else r->cur = line + strlen(line);
        r->lineno++;
        char *t = trim(line);
        if (*t == '\0' || *t == '#') continue;
        return t;
    }
    return NULL;
}

static void csv_close(csv_reader *r) { ev_free(r->buf); r->buf = r->cur = NULL; }

/* ------------------------------------------------------------ id index */
typedef struct { int id, pos; } idpos;
static int cmp_idpos(const void *a, const void *b) {
    const idpos *x = (const idpos *)a, *y = (const idpos *)b;
    return (x->id > y->id) - (x->id < y->id);
}
static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

int ev_node_index(const ev_scenario *sc, int id) {
    long k = ev_bsearch_iter(&id, sc->idx_ids, (size_t)sc->nn, sizeof(int), cmp_int);
    return k < 0 ? -1 : sc->idx_pos[k];
}

/* returns first duplicated value or INT_MIN-ish sentinel 0 flag */
static int find_duplicate(const int *ids, int n, int *dup) {
    if (n < 2) return 0;
    int *c = (int *)ev_malloc(sizeof(int) * (size_t)n);
    memcpy(c, ids, sizeof(int) * (size_t)n);
    ev_merge_sort(c, (size_t)n, sizeof(int), cmp_int);
    int found = 0;
    for (int i = 1; i < n; i++) if (c[i] == c[i - 1]) { *dup = c[i]; found = 1; break; }
    ev_free(c);
    return found;
}

/* ------------------------------------------------------------ section loaders */
static int load_config(const char *text, const char *path, ev_config *cfg, char *err, size_t cap) {
    csv_reader r;
    if (!csv_open_text(&r, text, path)) { seterr(err, cap, "%s: cannot open", path); return -1; }
    char *line;
    while ((line = csv_next(&r))) {
        char *eq = strchr(line, '=');
        if (!eq) { seterr(err, cap, "%s:%d: expected key = value", path, r.lineno); csv_close(&r); return -1; }
        *eq = '\0';
        char *k = trim(line), *v = trim(eq + 1);
        double d = 0;
        long iv = 0;
        int ok = 1;
#define DBL(key, field) else if (!strcmp(k, key)) { ok = parse_dbl(v, &d); cfg->field = d; }
#define INT(key, field) else if (!strcmp(k, key)) { ok = parse_int(v, &iv); cfg->field = (int)iv; }
        if (!strcmp(k, "name")) snprintf(cfg->name, sizeof cfg->name, "%s", v);
        INT("budget", budget)
        DBL("battery_kwh", battery_kwh)
        DBL("consumption_kwh_per_km", cons_kwh_km)
        DBL("reserve_frac", reserve_frac)
        DBL("target_frac", target_frac)
        DBL("regen_eff", regen_eff)
        DBL("elev_kwh_per_m", elev_kwh_per_m)
        DBL("speed_kmh", speed_kmh)
        DBL("max_detour_km", max_detour_km)
        DBL("grid_kw", grid_kw)
        INT("window_min", window_min)
        INT("session_min", session_min)
        INT("horizon_h", horizon_h)
        else { seterr(err, cap, "%s:%d: unknown key '%s'", path, r.lineno, k); csv_close(&r); return -1; }
#undef DBL
#undef INT
        if (!ok) { seterr(err, cap, "%s:%d: bad value for '%s': '%s'", path, r.lineno, k, v); csv_close(&r); return -1; }
    }
    csv_close(&r);
    const char *bad = NULL;
    if (cfg->budget < 0) bad = "budget must be >= 0";
    else if (cfg->battery_kwh <= 0) bad = "battery_kwh must be > 0";
    else if (cfg->cons_kwh_km <= 0) bad = "consumption_kwh_per_km must be > 0";
    else if (cfg->reserve_frac < 0 || cfg->reserve_frac >= cfg->target_frac || cfg->target_frac > 1)
        bad = "need 0 <= reserve_frac < target_frac <= 1";
    else if (cfg->regen_eff < 0 || cfg->regen_eff > 1) bad = "regen_eff must be in [0,1]";
    else if (cfg->elev_kwh_per_m < 0) bad = "elev_kwh_per_m must be >= 0";
    else if (cfg->speed_kmh <= 0) bad = "speed_kmh must be > 0";
    else if (cfg->max_detour_km < 0) bad = "max_detour_km must be >= 0";
    else if (cfg->window_min <= 0 || cfg->session_min <= 0) bad = "window_min and session_min must be > 0";
    else if (cfg->horizon_h < 1 || cfg->horizon_h > 240) bad = "horizon_h must be in [1,240]";
    if (bad) { seterr(err, cap, "%s: %s", path, bad); return -1; }
    return 0;
}

static int header_ok(char *line, const char *first) {
    char *f[1];
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", line);
    split(tmp, f, 1);
    return strcmp(f[0], first) == 0;
}

static int load_nodes(const char *text, const char *path, ev_scenario *sc, char *err, size_t cap) {
    csv_reader r;
    if (!csv_open_text(&r, text, path)) { seterr(err, cap, "%s: cannot open", path); return -1; }
    char *line = csv_next(&r);
    if (!line || !header_ok(line, "id")) { seterr(err, cap, "%s: missing header 'id,name,x_km,y_km,elev_m,demand'", path); csv_close(&r); return -1; }
    int capn = 256;
    sc->nodes = (ev_node *)ev_malloc(sizeof(ev_node) * (size_t)capn);
    while ((line = csv_next(&r))) {
        char *f[8];
        int nf = split(line, f, 8);
        if (nf != 6) { seterr(err, cap, "%s:%d: expected 6 fields, got %d", path, r.lineno, nf); csv_close(&r); return -1; }
        ev_node nd;
        long id, dem;
        if (!parse_int(f[0], &id) || !parse_dbl(f[2], &nd.x) || !parse_dbl(f[3], &nd.y) ||
            !parse_dbl(f[4], &nd.elev) || !parse_int(f[5], &dem)) {
            seterr(err, cap, "%s:%d: malformed number", path, r.lineno); csv_close(&r); return -1;
        }
        if (!*f[1]) { seterr(err, cap, "%s:%d: empty node name", path, r.lineno); csv_close(&r); return -1; }
        if (dem < 0) { seterr(err, cap, "%s:%d: demand must be >= 0", path, r.lineno); csv_close(&r); return -1; }
        nd.id = (int)id;
        nd.demand = (int)dem;
        snprintf(nd.name, sizeof nd.name, "%s", f[1]);
        if (sc->nn == capn) { capn *= 2; sc->nodes = (ev_node *)ev_realloc(sc->nodes, sizeof(ev_node) * (size_t)capn); }
        sc->nodes[sc->nn++] = nd;
    }
    csv_close(&r);
    if (sc->nn < 2) { seterr(err, cap, "%s: need at least 2 nodes", path); return -1; }
    /* domain index: (id,pos) sorted with the student merge sort */
    idpos *ip = (idpos *)ev_malloc(sizeof(idpos) * (size_t)sc->nn);
    for (int i = 0; i < sc->nn; i++) { ip[i].id = sc->nodes[i].id; ip[i].pos = i; }
    ev_merge_sort(ip, (size_t)sc->nn, sizeof(idpos), cmp_idpos);
    sc->idx_ids = (int *)ev_malloc(sizeof(int) * (size_t)sc->nn);
    sc->idx_pos = (int *)ev_malloc(sizeof(int) * (size_t)sc->nn);
    for (int i = 0; i < sc->nn; i++) {
        if (i > 0 && ip[i].id == ip[i - 1].id) {
            seterr(err, cap, "%s: duplicate node id %d", path, ip[i].id);
            ev_free(ip);
            return -1;
        }
        sc->idx_ids[i] = ip[i].id;
        sc->idx_pos[i] = ip[i].pos;
    }
    ev_free(ip);
    return 0;
}

static int load_edges(const char *text, const char *path, ev_scenario *sc, char *err, size_t cap) {
    csv_reader r;
    if (!csv_open_text(&r, text, path)) { seterr(err, cap, "%s: cannot open", path); return -1; }
    char *line = csv_next(&r);
    if (!line || !header_ok(line, "u")) { seterr(err, cap, "%s: missing header 'u,v,km,capacity'", path); csv_close(&r); return -1; }
    int capn = 512;
    sc->edges = (ev_edge *)ev_malloc(sizeof(ev_edge) * (size_t)capn);
    while ((line = csv_next(&r))) {
        char *f[6];
        int nf = split(line, f, 6);
        if (nf != 4) { seterr(err, cap, "%s:%d: expected 4 fields, got %d", path, r.lineno, nf); csv_close(&r); return -1; }
        long u, v, c;
        double km;
        if (!parse_int(f[0], &u) || !parse_int(f[1], &v) || !parse_dbl(f[2], &km) || !parse_int(f[3], &c)) {
            seterr(err, cap, "%s:%d: malformed number", path, r.lineno); csv_close(&r); return -1;
        }
        int ui = ev_node_index(sc, (int)u), vi = ev_node_index(sc, (int)v);
        if (ui < 0 || vi < 0) { seterr(err, cap, "%s:%d: unknown node id %ld", path, r.lineno, ui < 0 ? u : v); csv_close(&r); return -1; }
        if (ui == vi) { seterr(err, cap, "%s:%d: self-loop on node %ld", path, r.lineno, u); csv_close(&r); return -1; }
        if (km <= 0) { seterr(err, cap, "%s:%d: km must be > 0", path, r.lineno); csv_close(&r); return -1; }
        if (c < 0) { seterr(err, cap, "%s:%d: capacity must be >= 0", path, r.lineno); csv_close(&r); return -1; }
        if (sc->ne == capn) { capn *= 2; sc->edges = (ev_edge *)ev_realloc(sc->edges, sizeof(ev_edge) * (size_t)capn); }
        ev_edge e = { ui, vi, km, (int)c };
        sc->edges[sc->ne++] = e;
    }
    csv_close(&r);
    if (sc->ne < 1) { seterr(err, cap, "%s: need at least 1 edge", path); return -1; }
    return 0;
}

static int load_stations(const char *text, const char *path, ev_scenario *sc, char *err, size_t cap) {
    csv_reader r;
    if (!csv_open_text(&r, text, path)) { seterr(err, cap, "%s: cannot open", path); return -1; }
    char *line = csv_next(&r);
    if (!line || !header_ok(line, "id")) { seterr(err, cap, "%s: missing header 'id,node,cost,ports,power_kw[,maint_period_h,maint_offset_h]'", path); csv_close(&r); return -1; }
    int capn = 128;
    sc->stations = (ev_station *)ev_malloc(sizeof(ev_station) * (size_t)capn);
    while ((line = csv_next(&r))) {
        char *f[9];
        int nf = split(line, f, 9);
        if (nf != 5 && nf != 7) { seterr(err, cap, "%s:%d: expected 5 or 7 fields, got %d", path, r.lineno, nf); csv_close(&r); return -1; }
        long id, node, cost, ports, mp = 24, mo = 0;
        double pw;
        if (!parse_int(f[0], &id) || !parse_int(f[1], &node) || !parse_int(f[2], &cost) ||
            !parse_int(f[3], &ports) || !parse_dbl(f[4], &pw) ||
            (nf == 7 && (!parse_int(f[5], &mp) || !parse_int(f[6], &mo)))) {
            seterr(err, cap, "%s:%d: malformed number", path, r.lineno); csv_close(&r); return -1;
        }
        int ni = ev_node_index(sc, (int)node);
        const char *bad = NULL;
        if (ni < 0) bad = "unknown node id";
        else if (cost <= 0) bad = "cost must be > 0";
        else if (ports < 1) bad = "ports must be >= 1";
        else if (pw <= 0) bad = "power_kw must be > 0";
        else if (mp < 1 || mo < 0 || mo >= mp) bad = "need maint_period_h >= 1 and 0 <= maint_offset_h < period";
        if (bad) { seterr(err, cap, "%s:%d: %s", path, r.lineno, bad); csv_close(&r); return -1; }
        if (sc->ns == capn) { capn *= 2; sc->stations = (ev_station *)ev_realloc(sc->stations, sizeof(ev_station) * (size_t)capn); }
        ev_station s = { (int)id, ni, (int)cost, (int)ports, pw, (int)mp, (int)mo, 0 };
        sc->stations[sc->ns++] = s;
    }
    csv_close(&r);
    int *ids = (int *)ev_malloc(sizeof(int) * (size_t)(sc->ns ? sc->ns : 1)), dup;
    for (int i = 0; i < sc->ns; i++) ids[i] = sc->stations[i].id;
    int has = find_duplicate(ids, sc->ns, &dup);
    ev_free(ids);
    if (has) { seterr(err, cap, "%s: duplicate station id %d", path, dup); return -1; }
    return 0;
}

static int load_trips(const char *text, const char *path, ev_scenario *sc, char *err, size_t cap) {
    csv_reader r;
    if (!csv_open_text(&r, text, path)) { seterr(err, cap, "%s: cannot open", path); return -1; }
    char *line = csv_next(&r);
    if (!line || !header_ok(line, "id")) { seterr(err, cap, "%s: missing header 'id,src,dst,depart_min,soc0'", path); csv_close(&r); return -1; }
    int capn = 256;
    sc->trips = (ev_trip *)ev_malloc(sizeof(ev_trip) * (size_t)capn);
    while ((line = csv_next(&r))) {
        char *f[7];
        int nf = split(line, f, 7);
        if (nf != 5) { seterr(err, cap, "%s:%d: expected 5 fields, got %d", path, r.lineno, nf); csv_close(&r); return -1; }
        long id, s, d, dep;
        double soc;
        if (!parse_int(f[0], &id) || !parse_int(f[1], &s) || !parse_int(f[2], &d) ||
            !parse_int(f[3], &dep) || !parse_dbl(f[4], &soc)) {
            seterr(err, cap, "%s:%d: malformed number", path, r.lineno); csv_close(&r); return -1;
        }
        int si = ev_node_index(sc, (int)s), di = ev_node_index(sc, (int)d);
        const char *bad = NULL;
        if (si < 0 || di < 0) bad = "unknown node id";
        else if (si == di) bad = "src and dst must differ";
        else if (dep < 0) bad = "depart_min must be >= 0";
        else if (!(soc > 0 && soc <= 1)) bad = "soc0 must be in (0,1]";
        if (bad) { seterr(err, cap, "%s:%d: %s", path, r.lineno, bad); csv_close(&r); return -1; }
        if (sc->nt == capn) { capn *= 2; sc->trips = (ev_trip *)ev_realloc(sc->trips, sizeof(ev_trip) * (size_t)capn); }
        ev_trip t = { (int)id, si, di, (int)dep, soc };
        sc->trips[sc->nt++] = t;
    }
    csv_close(&r);
    int *ids = (int *)ev_malloc(sizeof(int) * (size_t)(sc->nt ? sc->nt : 1)), dup;
    for (int i = 0; i < sc->nt; i++) ids[i] = sc->trips[i].id;
    int has = find_duplicate(ids, sc->nt, &dup);
    ev_free(ids);
    if (has) { seterr(err, cap, "%s: duplicate trip id %d", path, dup); return -1; }
    return 0;
}

static void load_events(const char *text, ev_scenario *sc) {
    if (!text) { sc->events = (char *)ev_calloc(1, 1); sc->events_len = 0; return; }
    long len = (long)strlen(text);
    char *buf = (char *)ev_malloc((size_t)len + 1);
    memcpy(buf, text, (size_t)len + 1);
    long w = 0;
    for (long i = 0; i < len; i++) if (!isspace((unsigned char)buf[i])) buf[w++] = buf[i];
    buf[w] = '\0';
    sc->events = buf;
    sc->events_len = w;
}

static void build_graphs(ev_scenario *sc) {
    sc->na = 2 * sc->ne;
    sc->arc_src = (int *)ev_malloc(sizeof(int) * (size_t)sc->na);
    sc->arc_dst = (int *)ev_malloc(sizeof(int) * (size_t)sc->na);
    sc->arc_km = (double *)ev_malloc(sizeof(double) * (size_t)sc->na);
    sc->arc_kwh = (double *)ev_malloc(sizeof(double) * (size_t)sc->na);
    for (int e = 0; e < sc->ne; e++) {
        const ev_edge *ed = &sc->edges[e];
        double dz = sc->nodes[ed->v].elev - sc->nodes[ed->u].elev;
        sc->arc_src[2 * e] = ed->u; sc->arc_dst[2 * e] = ed->v;
        sc->arc_src[2 * e + 1] = ed->v; sc->arc_dst[2 * e + 1] = ed->u;
        sc->arc_km[2 * e] = sc->arc_km[2 * e + 1] = ed->km;
        sc->arc_kwh[2 * e] = ev_arc_energy(&sc->cfg, ed->km, dz);
        sc->arc_kwh[2 * e + 1] = ev_arc_energy(&sc->cfg, ed->km, -dz);
    }
    sc->g_km = ev_graph_build(sc->nn, sc->na, sc->arc_src, sc->arc_dst, sc->arc_km);
    sc->g_kwh = ev_graph_build(sc->nn, sc->na, sc->arc_src, sc->arc_dst, sc->arc_kwh);
}

int ev_scenario_load_mem(const ev_sources *src, ev_scenario *sc, char *err, size_t errcap) {
    memset(sc, 0, sizeof *sc);
    ev_config_defaults(&sc->cfg);
    const char *lb = src->label ? src->label : "scenario";
    char p[5][600];
    snprintf(p[0], sizeof p[0], "%s/scenario.cfg", lb);
    snprintf(p[1], sizeof p[1], "%s/nodes.csv", lb);
    snprintf(p[2], sizeof p[2], "%s/edges.csv", lb);
    snprintf(p[3], sizeof p[3], "%s/stations.csv", lb);
    snprintf(p[4], sizeof p[4], "%s/trips.csv", lb);
    if (load_config(src->cfg, p[0], &sc->cfg, err, errcap) ||
        load_nodes(src->nodes, p[1], sc, err, errcap) ||
        load_edges(src->edges, p[2], sc, err, errcap) ||
        load_stations(src->stations, p[3], sc, err, errcap) ||
        load_trips(src->trips, p[4], sc, err, errcap)) {
        ev_scenario_free(sc);
        return -1;
    }
    load_events(src->events, sc);
    build_graphs(sc);
    return 0;
}

int ev_scenario_load(const char *dir, ev_scenario *sc, char *err, size_t errcap) {
    static const char *files[6] = { "scenario.cfg", "nodes.csv", "edges.csv", "stations.csv", "trips.csv", "events.txt" };
    char *text[6];
    for (int i = 0; i < 6; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        text[i] = read_file(path, NULL);
    }
    ev_sources src = { dir, text[0], text[1], text[2], text[3], text[4], text[5] };
    int rc = ev_scenario_load_mem(&src, sc, err, errcap);
    for (int i = 0; i < 6; i++) ev_free(text[i]);
    return rc;
}

void ev_scenario_free(ev_scenario *sc) {
    ev_free(sc->nodes); ev_free(sc->edges); ev_free(sc->stations); ev_free(sc->trips);
    ev_free(sc->events); ev_free(sc->idx_ids); ev_free(sc->idx_pos);
    ev_free(sc->arc_src); ev_free(sc->arc_dst); ev_free(sc->arc_km); ev_free(sc->arc_kwh);
    ev_graph_free(sc->g_km); ev_graph_free(sc->g_kwh);
    memset(sc, 0, sizeof *sc);
}
