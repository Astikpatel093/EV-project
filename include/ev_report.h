/* ev_report.h - exports (CSV / JSON), strategy comparison and the full analysis report
 * that exercises every syllabus module on a loaded scenario. */
#ifndef EV_REPORT_H
#define EV_REPORT_H

#include <stdio.h>
#include "ev_plan.h"

/* ---- minimal streaming JSON writer */
typedef struct { FILE *f; int depth; int first[64]; } ev_json;
void ev_json_open(ev_json *j, FILE *f);
void ev_json_obj(ev_json *j, const char *key);   /* key NULL inside arrays / at root */
void ev_json_end_obj(ev_json *j);
void ev_json_arr(ev_json *j, const char *key);
void ev_json_end_arr(ev_json *j);
void ev_json_num(ev_json *j, const char *key, double v);
void ev_json_int(ev_json *j, const char *key, long long v);
void ev_json_str(ev_json *j, const char *key, const char *v);
void ev_json_bool(ev_json *j, const char *key, int v);

int ev_export_selection_csv(const char *path, const ev_scenario *sc, const ev_selection *sel);
int ev_export_plans_csv(const char *path, const ev_scenario *sc, const ev_run *run);
int ev_export_summary_json(const char *path, const ev_scenario *sc, const ev_run *run,
                           const ev_capacity *cap, const ev_sessions *ses);

void ev_print_run(FILE *out, const ev_scenario *sc, const ev_run *run,
                  const ev_capacity *cap, const ev_sessions *ses);

/* Runs every selection x routing strategy; prints a table and optionally writes CSV. */
int ev_compare(ev_scenario *sc, FILE *out, const char *csv_path);

/* Full syllabus analysis on the scenario; writes text to out and JSON to json_path (may be NULL). */
int ev_analyze(ev_scenario *sc, FILE *out, const char *json_path);
/* Same, writing JSON to an open stream (may be NULL). */
int ev_analyze_fp(ev_scenario *sc, FILE *out, FILE *json_out);

#endif
