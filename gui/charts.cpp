// charts.cpp - line, bar and battery charts drawn directly with Dear ImGui's ImDrawList.
//
// No plotting library: each chart maps data to pixels with two small lambdas (X, Y), draws grid
// lines with tick labels, then the data. Logarithmic axes map log10(value).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui.h"

namespace ev33 {

namespace {

struct Axis {
    double lo = 0, hi = 1;
    bool log = false;
    double map(double v) const { return log ? std::log10(std::max(v, 1e-12)) : v; }
};

// "nice" tick step for a linear axis: 1, 2 or 5 times a power of ten
double niceStep(double span, int target) {
    double raw = span / std::max(1, target);
    double p = std::pow(10.0, std::floor(std::log10(raw)));
    double m = raw / p;
    return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

std::string tickText(double v, bool log) {
    if (log) {
        double a = std::pow(10.0, v);
        if (a >= 1000) return fint(a);
        char b[32];
        std::snprintf(b, sizeof b, "%g", a);
        return b;
    }
    if (std::fabs(v) >= 1000) return fint(v);
    char b[32];
    std::snprintf(b, sizeof b, "%g", std::fabs(v) < 1e-12 ? 0.0 : v);
    return b;
}

std::vector<double> ticks(const Axis &a, int target) {
    std::vector<double> t;
    if (a.log) {
        for (double e = std::ceil(a.lo - 1e-9); e <= a.hi + 1e-9; e += 1) t.push_back(e);
        return t;
    }
    double step = niceStep(a.hi - a.lo, target);
    for (double v = std::ceil(a.lo / step - 1e-9) * step; v <= a.hi + step * 1e-6; v += step) t.push_back(v);
    return t;
}

} // namespace

void LineChart(App &app, const char *id, ImVec2 size, const std::vector<Series> &series, const ChartOptions &o) {
    const Palette &P = app.pal;
    float S = app.scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    ImFont *f = app.fonts.small;
    float fs = f->FontSize;

    Axis ax, ay;
    ax.log = o.logX;
    ay.log = o.logY;
    bool any = false;
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (const Series &s : series)
        for (size_t i = 0; i < s.x.size(); i++) {
            if ((o.logX && s.x[i] <= 0) || (o.logY && s.y[i] <= 0)) continue;
            any = true;
            x0 = std::min(x0, ax.map(s.x[i])); x1 = std::max(x1, ax.map(s.x[i]));
            y0 = std::min(y0, ay.map(s.y[i])); y1 = std::max(y1, ay.map(s.y[i]));
        }
    if (o.hLine >= 0) { y0 = std::min(y0, ay.map(o.hLine)); y1 = std::max(y1, ay.map(o.hLine)); }
    if (!any) {
        dl->AddText(f, fs, ImVec2(p0.x + 10 * S, p0.y + 10 * S), col(P.ink3), "No data to plot.");
        return;
    }
    if (o.logX) { x0 = std::floor(x0); x1 = std::ceil(x1); }
    if (o.logY) { y0 = std::floor(y0); y1 = std::ceil(y1); }
    else if (o.yMinZero) y0 = std::min(0.0, y0);
    if (x1 - x0 < 1e-12) { x0 -= 1; x1 += 1; }
    if (y1 - y0 < 1e-12) { y0 -= 1; y1 += 1; }
    if (!o.logY) y1 += (y1 - y0) * 0.06;
    ax.lo = x0; ax.hi = x1; ay.lo = y0; ay.hi = y1;

    float left = 54 * S, right = 26 * S, top = fs + 10 * S, bottom = 34 * S;
    ImVec2 a(p0.x + left, p0.y + top), b(p0.x + size.x - right, p0.y + size.y - bottom);
    auto X = [&](double v) { return a.x + (float)((ax.map(v) - ax.lo) / (ax.hi - ax.lo)) * (b.x - a.x); };
    auto Y = [&](double v) { return b.y - (float)((ay.map(v) - ay.lo) / (ay.hi - ay.lo)) * (b.y - a.y); };
    auto Xr = [&](double m) { return a.x + (float)((m - ax.lo) / (ax.hi - ax.lo)) * (b.x - a.x); };
    auto Yr = [&](double m) { return b.y - (float)((m - ay.lo) / (ay.hi - ay.lo)) * (b.y - a.y); };

    dl->AddRectFilled(a, b, col(P.panel));
    for (double t : ticks(ax, 6)) {
        float x = Xr(t);
        dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), col(P.line, 0.7f));
        std::string s = tickText(t, ax.log);
        float w = f->CalcTextSizeA(fs, 1e9f, 0, s.c_str()).x;
        dl->AddText(f, fs, ImVec2(x - w / 2, b.y + 4 * S), col(P.ink3), s.c_str());
    }
    for (double t : ticks(ay, 5)) {
        float y = Yr(t);
        dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col(P.line, 0.7f));
        std::string s = tickText(t, ay.log);
        float w = f->CalcTextSizeA(fs, 1e9f, 0, s.c_str()).x;
        dl->AddText(f, fs, ImVec2(a.x - w - 6 * S, y - fs / 2), col(P.ink3), s.c_str());
    }
    dl->AddRect(a, b, col(P.line));
    if (o.xLabel && *o.xLabel) {
        float w = f->CalcTextSizeA(fs, 1e9f, 0, o.xLabel).x;
        dl->AddText(f, fs, ImVec2((a.x + b.x - w) / 2, b.y + 6 * S + fs), col(P.ink2), o.xLabel);
    }
    if (o.yLabel && *o.yLabel) dl->AddText(f, fs, ImVec2(p0.x, p0.y - 2 * S), col(P.ink2), o.yLabel);
    if (o.hLine >= 0) {
        float y = Y(o.hLine);
        for (float x = a.x; x < b.x; x += 9 * S) dl->AddLine(ImVec2(x, y), ImVec2(std::min(b.x, x + 5 * S), y), col(P.ink2), 1.2f * S);
        if (o.hLineLabel) dl->AddText(f, fs, ImVec2(b.x - f->CalcTextSizeA(fs, 1e9f, 0, o.hLineLabel).x - 4 * S, y - fs - 2 * S), col(P.ink2), o.hLineLabel);
    }
    if (o.vLine >= 0) dl->AddLine(ImVec2(X(o.vLine), a.y), ImVec2(X(o.vLine), b.y), col(P.warn), 1.6f * S);

    dl->PushClipRect(a, b, true);
    int hs = -1;
    size_t hi = 0;
    float best = 14 * S;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    for (size_t k = 0; k < series.size(); k++) {
        const Series &s = series[k];
        std::vector<ImVec2> pts;
        for (size_t i = 0; i < s.x.size(); i++) {
            if ((o.logX && s.x[i] <= 0) || (o.logY && s.y[i] <= 0)) continue;
            pts.push_back(ImVec2(X(s.x[i]), Y(s.y[i])));
            if (hovered) {
                float d = std::hypot(pts.back().x - mouse.x, pts.back().y - mouse.y);
                if (d < best) { best = d; hs = (int)k; hi = i; }
            }
        }
        if (pts.size() >= 2) dl->AddPolyline(pts.data(), (int)pts.size(), col(s.color), 0, 2.0f * S);
        if (pts.size() < 40)
            for (const ImVec2 &q : pts) dl->AddCircleFilled(q, 3.0f * S, col(s.color));
    }
    dl->PopClipRect();
    if (hs >= 0) {
        const Series &s = series[(size_t)hs];
        ImGui::SetTooltip("%s\n%s: %s\n%s: %s", s.name.c_str(), *o.xLabel ? o.xLabel : "x", fint(s.x[hi]).c_str(),
                          *o.yLabel ? o.yLabel : "y", (s.y[hi] < 10 ? f2(s.y[hi]) : fint(s.y[hi])).c_str());
    }
}

void BarChart(App &app, const char *id, ImVec2 size, const std::vector<std::string> &labels, const std::vector<double> &values,
              const std::vector<double> *over, const ImVec4 &color, const ImVec4 &overColor, const ChartOptions &o) {
    const Palette &P = app.pal;
    float S = app.scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    ImFont *f = app.fonts.small;
    float fs = f->FontSize;
    size_t n = values.size();
    if (n == 0) {
        dl->AddText(f, fs, ImVec2(p0.x + 10 * S, p0.y + 10 * S), col(P.ink3), "No data to plot.");
        return;
    }
    double top = 0;
    for (size_t i = 0; i < n; i++) top = std::max(top, values[i] + (over ? (*over)[i] : 0));
    if (o.hLine > top) top = o.hLine;
    if (top <= 0) top = 1;
    top *= 1.08;
    Axis ay;
    ay.lo = 0;
    ay.hi = top;
    float left = 54 * S, right = 10 * S, topPad = fs + 10 * S, bottom = 34 * S;
    ImVec2 a(p0.x + left, p0.y + topPad), b(p0.x + size.x - right, p0.y + size.y - bottom);
    auto Y = [&](double v) { return b.y - (float)(v / top) * (b.y - a.y); };
    dl->AddRectFilled(a, b, col(P.panel));
    for (double t : ticks(ay, 5)) {
        float y = Y(t);
        dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col(P.line, 0.7f));
        std::string s = tickText(t, false);
        float w = f->CalcTextSizeA(fs, 1e9f, 0, s.c_str()).x;
        dl->AddText(f, fs, ImVec2(a.x - w - 6 * S, y - fs / 2), col(P.ink3), s.c_str());
    }
    float slot = (b.x - a.x) / (float)n, bw = std::max(2.0f, slot * 0.72f);
    int every = std::max(1, (int)std::ceil(36 * S / slot));
    int hov = -1;
    for (size_t i = 0; i < n; i++) {
        float cx = a.x + slot * ((float)i + 0.5f);
        float yb = Y(values[i]);
        dl->AddRectFilled(ImVec2(cx - bw / 2, yb), ImVec2(cx + bw / 2, b.y), col(color));
        if (over && (*over)[i] > 0) dl->AddRectFilled(ImVec2(cx - bw / 2, Y(values[i] + (*over)[i])), ImVec2(cx + bw / 2, yb), col(overColor));
        if ((int)i % every == 0 && i < labels.size()) {
            float w = f->CalcTextSizeA(fs, 1e9f, 0, labels[i].c_str()).x;
            dl->AddText(f, fs, ImVec2(cx - w / 2, b.y + 4 * S), col(P.ink3), labels[i].c_str());
        }
        ImVec2 m = ImGui::GetIO().MousePos;
        if (hovered && m.x >= cx - slot / 2 && m.x < cx + slot / 2) hov = (int)i;
    }
    dl->AddRect(a, b, col(P.line));
    if (o.hLine >= 0) {
        float y = Y(o.hLine);
        for (float x = a.x; x < b.x; x += 9 * S) dl->AddLine(ImVec2(x, y), ImVec2(std::min(b.x, x + 5 * S), y), col(P.ink), 1.2f * S);
        if (o.hLineLabel) dl->AddText(f, fs, ImVec2(b.x - f->CalcTextSizeA(fs, 1e9f, 0, o.hLineLabel).x - 4 * S, y - fs - 2 * S), col(P.ink2), o.hLineLabel);
    }
    if (o.xLabel && *o.xLabel) {
        float w = f->CalcTextSizeA(fs, 1e9f, 0, o.xLabel).x;
        dl->AddText(f, fs, ImVec2((a.x + b.x - w) / 2, b.y + 6 * S + fs), col(P.ink2), o.xLabel);
    }
    if (o.yLabel && *o.yLabel) dl->AddText(f, fs, ImVec2(p0.x, p0.y - 2 * S), col(P.ink2), o.yLabel);
    if (hov >= 0) {
        double extra = over ? (*over)[(size_t)hov] : 0;
        ImGui::SetTooltip("%s: %s%s", hov < (int)labels.size() ? labels[(size_t)hov].c_str() : "", fint(values[(size_t)hov] + extra).c_str(),
                          extra > 0 ? ("  (" + fint(extra) + " above the line)").c_str() : "");
    }
}

void BatteryChart(App &app, const char *id, ImVec2 size, const std::vector<std::array<double, 3>> &profile, double capacity, double reserve,
                  double target) {
    const Palette &P = app.pal;
    float S = app.scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    ImFont *f = app.fonts.small;
    float fs = f->FontSize;
    if (profile.size() < 2 || capacity <= 0) {
        dl->AddText(f, fs, ImVec2(p0.x + 10 * S, p0.y + 10 * S), col(P.ink3), "No route to chart.");
        return;
    }
    double total = std::max(1e-6, profile.back()[0]);
    double emin = 1e300, emax = -1e300;
    for (auto &q : profile) { emin = std::min(emin, q[2]); emax = std::max(emax, q[2]); }
    if (emax - emin < 1) emax = emin + 1;
    float left = 44 * S, right = 10 * S, top = fs + 10 * S, bottom = 30 * S;
    ImVec2 a(p0.x + left, p0.y + top), b(p0.x + size.x - right, p0.y + size.y - bottom);
    auto X = [&](double km) { return a.x + (float)(km / total) * (b.x - a.x); };
    auto Y = [&](double kwh) { return b.y - (float)(std::max(0.0, kwh) / capacity) * (b.y - a.y); };
    auto YE = [&](double m) { return b.y - (float)((m - emin) / (emax - emin)) * (b.y - a.y) * 0.35f; };
    dl->AddRectFilled(a, b, col(P.panel));
    // elevation silhouette
    for (size_t i = 0; i + 1 < profile.size(); i++) {
        float xa = X(profile[i][0]), xb = X(profile[i + 1][0]);
        if (xb <= xa) continue;
        dl->AddQuadFilled(ImVec2(xa, b.y), ImVec2(xa, YE(profile[i][2])), ImVec2(xb, YE(profile[i + 1][2])), ImVec2(xb, b.y), col(P.contour, 0.55f));
    }
    Axis ay;
    ay.hi = capacity;
    for (double t : ticks(ay, 4)) {
        float y = Y(t);
        dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col(P.line, 0.7f));
        std::string s = tickText(t, false);
        float w = f->CalcTextSizeA(fs, 1e9f, 0, s.c_str()).x;
        dl->AddText(f, fs, ImVec2(a.x - w - 6 * S, y - fs / 2), col(P.ink3), s.c_str());
    }
    Axis ax;
    ax.hi = total;
    for (double t : ticks(ax, 6)) {
        std::string s = tickText(t, false);
        float w = f->CalcTextSizeA(fs, 1e9f, 0, s.c_str()).x;
        dl->AddText(f, fs, ImVec2(X(t) - w / 2, b.y + 4 * S), col(P.ink3), s.c_str());
    }
    dl->AddText(f, fs, ImVec2(b.x - f->CalcTextSizeA(fs, 1e9f, 0, "km").x, b.y + 6 * S + fs * 0.9f), col(P.ink2), "km");
    dl->AddText(f, fs, ImVec2(p0.x, p0.y - 2 * S), col(P.ink2), "kWh");
    float yr = Y(reserve), yt = Y(target);
    dl->AddLine(ImVec2(a.x, yr), ImVec2(b.x, yr), col(P.error), 1.4f * S);
    for (float x = a.x; x < b.x; x += 8 * S) dl->AddLine(ImVec2(x, yt), ImVec2(std::min(b.x, x + 4 * S), yt), col(P.ink3), 1.2f * S);
    std::vector<ImVec2> pts;
    for (auto &q : profile) pts.push_back(ImVec2(X(q[0]), Y(q[1])));
    dl->AddPolyline(pts.data(), (int)pts.size(), col(P.primary), 0, 2.2f * S);
    for (size_t i = 0; i < profile.size(); i++)
        if (profile[i][1] < reserve - 1e-9) { dl->AddCircleFilled(pts[i], 3.5f * S, col(P.error)); break; }   // first point below reserve
    dl->AddRect(a, b, col(P.line));
    if (hovered) {
        float mx = ImGui::GetIO().MousePos.x;
        size_t k = 0;
        for (size_t i = 0; i < pts.size(); i++) if (std::fabs(pts[i].x - mx) < std::fabs(pts[k].x - mx)) k = i;
        dl->AddLine(ImVec2(pts[k].x, a.y), ImVec2(pts[k].x, b.y), col(P.ink3));
        ImGui::SetTooltip("%s km: %s kWh, elevation %s m", f1(profile[k][0]).c_str(), f2(profile[k][1]).c_str(), fint(profile[k][2]).c_str());
    }
}

} // namespace ev33
