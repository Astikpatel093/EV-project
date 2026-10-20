/* ev33 - EV Charging Station Placement and Trip Planning System (CLI).
 *
 *   ev33 run      --data DIR [--select S] [--route R] [--out DIR]
 *   ev33 compare  --data DIR [--out DIR]
 *   ev33 analyze  --data DIR [--out DIR]
 *   ev33 plan     --data DIR --trip ID [--select S] [--route R]
 *   ev33 validate --data DIR
 *   ev33 menu     [--data DIR]
 *   ev33 demo     [--data ROOT] [--out DIR]   three scenarios, two+ strategies each, transcript
 *   ev33 help
 *
 *   S = greedy | dp | bnb-lc | bnb-fifo | bnb-lifo      (default dp)
 *   R = dijkstra | floyd | bellman                      (default dijkstra)
 */
#include "ev_mem.h"
#include "ev_model.h"
#include "ev_plan.h"
#include "ev_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

#define EV33_VERSION "1.0.0"

/* mkdir -p: create every missing component of path */
static void mkdir_p(const char *path) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/' || *p == '\\') { char c = *p; *p = '\0'; MKDIR(buf); *p = c; }
    MKDIR(buf);
}

typedef struct {
    const char *cmd, *data, *out;
    int sel, route, trip;
} options;

static void usage(FILE *o) {
    fprintf(o,
        "ev33 %s - EV charging station placement and trip planning\n\n"
        "usage:\n"
        "  ev33 run      --data DIR [--select S] [--route R] [--out DIR]   plan one strategy, export CSV/JSON\n"
        "  ev33 compare  --data DIR [--out DIR]                            every selection x routing strategy\n"
        "  ev33 analyze  --data DIR [--out DIR]                            full syllabus-module report\n"
        "  ev33 plan     --data DIR --trip ID [--select S] [--route R]     show one trip's charging plan\n"
        "  ev33 validate --data DIR                                        load + validate a scenario\n"
        "  ev33 menu     [--data DIR]                                      interactive menu\n"
        "  ev33 demo     [--data ROOT] [--out DIR]                         the three demonstration scenarios\n\n"
        "  S = greedy | dp | bnb-lc | bnb-fifo | bnb-lifo   (default dp)\n"
        "  R = dijkstra | floyd | bellman                   (default dijkstra)\n", EV33_VERSION);
}

static int parse_args(int argc, char **argv, options *op) {
    memset(op, 0, sizeof *op);
    op->sel = EV_SEL_DP;
    op->route = EV_ROUTE_DIJKSTRA;
    op->trip = -1;
    if (argc < 2) return -1;
    op->cmd = argv[1];
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--data") && v) { op->data = v; i++; }
        else if (!strcmp(a, "--out") && v) { op->out = v; i++; }
        else if (!strcmp(a, "--select") && v) {
            op->sel = ev_sel_parse(v); i++;
            if (op->sel < 0) { fprintf(stderr, "error: unknown selection strategy '%s'\n", v); return -2; }
        } else if (!strcmp(a, "--route") && v) {
            op->route = ev_route_parse(v); i++;
            if (op->route < 0) { fprintf(stderr, "error: unknown routing strategy '%s'\n", v); return -2; }
        } else if (!strcmp(a, "--trip") && v) {
            char *end;
            op->trip = (int)strtol(v, &end, 10); i++;
            if (*end) { fprintf(stderr, "error: --trip expects an integer id\n"); return -2; }
        } else { fprintf(stderr, "error: unexpected argument '%s'\n", a); return -2; }
    }
    return 0;
}

static int load(const char *dir, ev_scenario *sc) {
    char err[512];
    if (!dir) { fprintf(stderr, "error: --data DIR is required\n"); return -1; }
    if (ev_scenario_load(dir, sc, err, sizeof err) != 0) { fprintf(stderr, "error: %s\n", err); return -1; }
    return 0;
}

static void out_path(char *buf, size_t cap, const char *dir, const char *file) { snprintf(buf, cap, "%s/%s", dir, file); }

static int cmd_run(ev_scenario *sc, const options *op, FILE *o) {
    ev_run run;
    ev_run_pipeline(sc, (ev_sel_mode)op->sel, (ev_route_mode)op->route, &run);
    ev_capacity cap;
    ev_sessions ses;
    ev_capacity_analyze(run.router, sc, &run.sel, run.plans, &cap);
    ev_sessions_analyze(sc, &run.sel, run.plans, &ses);
    ev_print_run(o, sc, &run, &cap, &ses);
    if (op->out) {
        char p[1024];
        mkdir_p(op->out);
        out_path(p, sizeof p, op->out, "selection.csv");
        if (ev_export_selection_csv(p, sc, &run.sel)) fprintf(stderr, "warning: cannot write %s\n", p);
        out_path(p, sizeof p, op->out, "plans.csv");
        if (ev_export_plans_csv(p, sc, &run)) fprintf(stderr, "warning: cannot write %s\n", p);
        out_path(p, sizeof p, op->out, "summary.json");
        if (ev_export_summary_json(p, sc, &run, &cap, &ses)) fprintf(stderr, "warning: cannot write %s\n", p);
        fprintf(o, "   exported selection.csv, plans.csv, summary.json to %s\n", op->out);
    }
    ev_capacity_free(&cap);
    ev_sessions_free(&ses);
    ev_run_free(&run);
    return 0;
}

static int cmd_plan(ev_scenario *sc, const options *op, FILE *o) {
    int t = -1;
    for (int i = 0; i < sc->nt; i++) if (sc->trips[i].id == op->trip) t = i;
    if (t < 0) { fprintf(stderr, "error: trip %d not found\n", op->trip); return 1; }
    ev_selection sel;
    ev_select(sc, (ev_sel_mode)op->sel, &sel);
    ev_router *r = ev_router_create(sc, (ev_route_mode)op->route, sel.idx, sel.n);
    ev_plan p;
    int ok = ev_plan_trip(r, sc, t, &p);
    const ev_trip *tr = &sc->trips[t];
    const ev_config *c = &sc->cfg;
    fprintf(o, "trip %d: %s -> %s, departs %02d:%02d with %.0f%% SoC (%.1f kWh)\n", tr->id, sc->nodes[tr->src].name,
           sc->nodes[tr->dst].name, tr->depart_min / 60, tr->depart_min % 60, tr->soc0 * 100, tr->soc0 * c->battery_kwh);
    fprintf(o, "strategy: select=%s route=%s (%d stations built)\n", ev_sel_name(op->sel), ev_route_name(op->route), sel.n);
    if (p.direct_km >= 0) fprintf(o, "direct route: %.1f km%s\n", p.direct_km, p.needs_charge ? " (not reachable on current charge)" : "");
    if (!ok) fprintf(o, "result: INFEASIBLE - no chain of selected stations within battery range\n");
    else {
        fprintf(o, "result: %.1f km, %.2f kWh, %d charging stop(s), arrive %02d:%02d\n", p.km, p.kwh, p.n_stops,
               ((int)p.arrive_min / 60) % 24, (int)p.arrive_min % 60);
        for (int k = 0; k < p.n_stops; k++) {
            const ev_station *s = &sc->stations[p.stop[k]];
            fprintf(o, "  stop %d: station %d at %s, arrive %02d:%02d with %.1f kWh, charge %.0f min at %.0f kW\n", k + 1,
                   s->id, sc->nodes[s->node].name, ((int)p.stop_arrive_min[k] / 60) % 24, (int)p.stop_arrive_min[k] % 60,
                   p.stop_arrive_kwh[k], p.stop_charge_min[k], s->power_kw);
        }
        int *path = malloc(sizeof(int) * (size_t)sc->nn);
        int len = ev_router_path(r, tr->src, p.n_stops ? sc->stations[p.stop[0]].node : tr->dst, path, sc->nn);
        fprintf(o, "  first leg nodes:");
        for (int i = 0; i < len && i < 25; i++) fprintf(o, " %s", sc->nodes[path[i]].name);
        if (len > 25) fprintf(o, " ...");
        fprintf(o, "\n");
        free(path);
    }
    ev_router_free(r);
    ev_selection_free(&sel);
    return 0;
}

static int run_command(const options *op) {
    ev_scenario sc;
    if (!strcmp(op->cmd, "help") || !strcmp(op->cmd, "--help") || !strcmp(op->cmd, "-h")) { usage(stdout); return 0; }
    if (!strcmp(op->cmd, "version")) { printf("ev33 %s\n", EV33_VERSION); return 0; }
    if (load(op->data, &sc)) return 1;
    int rc = 0;
    if (!strcmp(op->cmd, "validate")) {
        printf("ok: '%s' %d nodes, %d roads, %d candidate sites, %d trips, %ld events, budget %d\n", sc.cfg.name, sc.nn, sc.ne,
               sc.ns, sc.nt, sc.events_len, sc.cfg.budget);
    } else if (!strcmp(op->cmd, "run")) rc = cmd_run(&sc, op, stdout);
    else if (!strcmp(op->cmd, "compare")) {
        char p[1024];
        if (op->out) { mkdir_p(op->out); out_path(p, sizeof p, op->out, "compare.csv"); }
        rc = ev_compare(&sc, stdout, op->out ? p : NULL);
        if (op->out) printf("   exported compare.csv to %s\n", op->out);
    } else if (!strcmp(op->cmd, "analyze")) {
        char p[1024];
        if (op->out) { mkdir_p(op->out); out_path(p, sizeof p, op->out, "analysis.json"); }
        rc = ev_analyze(&sc, stdout, op->out ? p : NULL);
        if (op->out) printf("\n   exported analysis.json to %s\n", op->out);
    } else if (!strcmp(op->cmd, "plan")) {
        if (op->trip < 0) { fprintf(stderr, "error: plan needs --trip ID\n"); rc = 1; }
        else rc = cmd_plan(&sc, op, stdout);
    } else { fprintf(stderr, "error: unknown command '%s'\n\n", op->cmd); usage(stderr); rc = 1; }
    ev_scenario_free(&sc);
    return rc;
}

/* ------------------------------------------------------------ demonstration */
/* Runs one step into a temporary file, then copies its output to the console and the transcript. */
static void demo_flush(FILE *tmp, FILE *log) {
    char buf[4096];
    size_t n;
    fflush(tmp);
    rewind(tmp);
    while ((n = fread(buf, 1, sizeof buf, tmp)) > 0) { fwrite(buf, 1, n, stdout); fwrite(buf, 1, n, log); }
    fflush(stdout);
}
static void demo_say(FILE *log, const char *fmt, const char *a, const char *b) {
    char line[512];
    snprintf(line, sizeof line, fmt, a, b);
    printf("\n######## %s\n", line);
    fprintf(log, "\n######## %s\n", line);
}

/* ev33 demo: normal operation, a capacity/constraint shock and a large-data stress test, each with
 * exact DP + Dijkstra against greedy + Bellman-Ford, the full strategy matrix, then the syllabus
 * analysis and a two-stop trip. Outputs go to OUT/<scenario>/<strategy>/, transcript to OUT/transcript.txt. */
static int cmd_demo(const options *op) {
    const char *root = op->data ? op->data : "data";
    const char *outdir = op->out ? op->out : "results/demo";
    const char *scen[3] = { "medium", "shock", "large" };
    char path[1200], dir[1024];
    mkdir_p(outdir);
    out_path(path, sizeof path, outdir, "transcript.txt");
    FILE *log = fopen(path, "w");
    if (!log) { fprintf(stderr, "error: cannot write %s\n", path); return 1; }
    for (int k = 0; k < 3; k++) {
        ev_scenario sc;
        snprintf(dir, sizeof dir, "%s/%s", root, scen[k]);
        demo_say(log, "SCENARIO %s: validate%s", scen[k], "");
        if (load(dir, &sc)) { fclose(log); return 1; }
        FILE *t = tmpfile();
        if (!t) { fclose(log); ev_scenario_free(&sc); return 1; }
        fprintf(t, "ok: '%s' %d nodes, %d roads, %d candidate sites, %d trips, %ld events, budget %d\n", sc.cfg.name, sc.nn, sc.ne,
                sc.ns, sc.nt, sc.events_len, sc.cfg.budget);
        demo_flush(t, log);
        fclose(t);

        options run = *op;
        run.data = dir;
        static const struct { int sel, route; const char *name, *label; } STRAT[2] = {
            { EV_SEL_DP, EV_ROUTE_DIJKSTRA, "dp_dijkstra", "strategy A = exact selection (DP) + Dijkstra routing" },
            { EV_SEL_GREEDY, EV_ROUTE_BELLMAN, "greedy_bellman", "strategy B = greedy selection + energy-optimal Bellman-Ford routing" },
        };
        for (int s = 0; s < 2; s++) {
            char out[1024];
            snprintf(out, sizeof out, "%s/%s/%s", outdir, scen[k], STRAT[s].name);
            demo_say(log, "SCENARIO %s: %s", scen[k], STRAT[s].label);
            run.sel = STRAT[s].sel;
            run.route = STRAT[s].route;
            run.out = out;
            t = tmpfile();
            cmd_run(&sc, &run, t);
            demo_flush(t, log);
            fclose(t);
        }
        demo_say(log, "SCENARIO %s: full strategy matrix (5 selection x 3 routing)%s", scen[k], "");
        snprintf(dir, sizeof dir, "%s/%s", outdir, scen[k]);
        mkdir_p(dir);
        out_path(path, sizeof path, dir, "compare.csv");
        t = tmpfile();
        ev_compare(&sc, t, path);
        fprintf(t, "   exported compare.csv to %s\n", dir);
        demo_flush(t, log);
        fclose(t);
        ev_scenario_free(&sc);
    }
    {
        ev_scenario sc;
        snprintf(dir, sizeof dir, "%s/medium", root);
        demo_say(log, "Syllabus analysis on the normal scenario%s%s", "", "");
        if (load(dir, &sc) == 0) {
            snprintf(path, sizeof path, "%s/medium/analysis.json", outdir);
            FILE *t = tmpfile();
            ev_analyze(&sc, t, path);
            fprintf(t, "\n   exported analysis.json to %s/medium\n", outdir);
            demo_flush(t, log);
            fclose(t);
            ev_scenario_free(&sc);
        }
        snprintf(dir, sizeof dir, "%s/large", root);
        demo_say(log, "Single-trip explanation (large map, two charging stops)%s%s", "", "");
        if (load(dir, &sc) == 0) {
            options pl = *op;
            pl.sel = EV_SEL_DP;
            pl.route = EV_ROUTE_DIJKSTRA;
            pl.trip = 22;
            FILE *t = tmpfile();
            cmd_plan(&sc, &pl, t);
            demo_flush(t, log);
            fclose(t);
            ev_scenario_free(&sc);
        }
    }
    out_path(path, sizeof path, outdir, "transcript.txt");
    printf("\nDemo complete. Transcript: %s\n", path);
    fclose(log);
    return 0;
}

/* ------------------------------------------------------------ interactive menu */
static void read_line(const char *prompt, char *buf, size_t cap) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)cap, stdin)) { buf[0] = 'q'; buf[1] = '\0'; return; }
    buf[strcspn(buf, "\r\n")] = '\0';
}

static int menu(const char *initial) {
    ev_scenario sc;
    int loaded = 0;
    char dir[512] = "", line[512];
    int sel = EV_SEL_DP, route = EV_ROUTE_DIJKSTRA;
    if (initial) {
        snprintf(dir, sizeof dir, "%s", initial);
        if (load(dir, &sc) == 0) { loaded = 1; printf("loaded %s\n", dir); }
    }
    for (;;) {
        printf("\n==== EV33 menu ====  data: %s | select: %s | route: %s\n", loaded ? dir : "(none)", ev_sel_name(sel), ev_route_name(route));
        printf(" 1) load scenario   2) choose selection   3) choose routing\n"
               " 4) run scenario    5) compare strategies 6) plan a trip\n"
               " 7) full analysis   8) export results     q) quit\n");
        read_line("> ", line, sizeof line);
        if (line[0] == 'q' || line[0] == 'Q') break;
        int ch = atoi(line);
        if (ch == 1) {
            read_line("scenario directory: ", line, sizeof line);
            ev_scenario tmp;
            if (load(line, &tmp) == 0) {
                if (loaded) ev_scenario_free(&sc);
                sc = tmp; loaded = 1;
                snprintf(dir, sizeof dir, "%s", line);
                printf("ok: %d nodes, %d sites, %d trips\n", sc.nn, sc.ns, sc.nt);
            }
            continue;
        }
        if (ch == 2) {
            read_line("selection (greedy|dp|bnb-lc|bnb-fifo|bnb-lifo): ", line, sizeof line);
            int s = ev_sel_parse(line);
            if (s < 0) printf("unknown strategy\n"); else sel = s;
            continue;
        }
        if (ch == 3) {
            read_line("routing (dijkstra|floyd|bellman): ", line, sizeof line);
            int r = ev_route_parse(line);
            if (r < 0) printf("unknown strategy\n"); else route = r;
            continue;
        }
        if (!loaded) { printf("load a scenario first (option 1)\n"); continue; }
        options op;
        memset(&op, 0, sizeof op);
        op.sel = sel; op.route = route; op.data = dir; op.trip = -1;
        if (ch == 4) cmd_run(&sc, &op, stdout);
        else if (ch == 5) ev_compare(&sc, stdout, NULL);
        else if (ch == 6) {
            read_line("trip id: ", line, sizeof line);
            op.trip = atoi(line);
            cmd_plan(&sc, &op, stdout);
        } else if (ch == 7) ev_analyze(&sc, stdout, NULL);
        else if (ch == 8) {
            read_line("output directory: ", line, sizeof line);
            op.out = line;
            cmd_run(&sc, &op, stdout);
        } else printf("unknown option\n");
    }
    if (loaded) ev_scenario_free(&sc);
    return 0;
}

int main(int argc, char **argv) {
    options op;
    int pr = parse_args(argc, argv, &op);
    if (pr == -1) { usage(stderr); return 1; }
    if (pr < 0) return 1;
    if (!strcmp(op.cmd, "menu")) return menu(op.data);
    if (!strcmp(op.cmd, "demo")) return cmd_demo(&op);
    return run_command(&op);
}
