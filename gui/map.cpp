// map.cpp - the city map: terrain, contours, roads, stations, a replayed trip or the live day.
#include <algorithm>
#include <cmath>
#include <string>

#include "ui.h"

namespace ev33 {

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

int drawCityMap(App &app, MapView &mv, const char *id, ImVec2 size, MapMode mode, const LiveState *live, const SimView *sim) {
    Engine &E = app.eng;
    City &C = E.city();
    const ev_scenario &sc = C.sc;
    const Palette &P = app.pal;
    Run *R = E.run();
    int picked = -1;

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float S = app.scale;

    dl->AddRectFilled(p0, p1, col(P.paper), 10 * S);
    dl->PushClipRect(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p1.y - 1), true);

    // data -> screen
    double dw = std::max(1e-6, C.maxx - C.minx), dh = std::max(1e-6, C.maxy - C.miny);
    float pad = 26 * S;
    float s0 = (float)std::min((size.x - 2 * pad) / dw, (size.y - 2 * pad) / dh);
    float cx = (p0.x + p1.x) / 2, cy = (p0.y + p1.y) / 2;
    double mx = (C.minx + C.maxx) / 2, my = (C.miny + C.maxy) / 2;
    auto X = [&](double x) { return cx + (float)((x - mx) * s0 * mv.z) + mv.tx; };
    auto Y = [&](double y) { return cy - (float)((y - my) * s0 * mv.z) + mv.ty; };

    // zoom with the wheel at the pointer, drag to pan, double-click to zoom in
    ImGuiIO &io = ImGui::GetIO();
    auto zoomAt = [&](float f, ImVec2 at) {
        float nz = clampf(mv.z * f, 1.f, 14.f);
        f = nz / mv.z;
        mv.tx = at.x - cx - (at.x - cx - mv.tx) * f;
        mv.ty = at.y - cy - (at.y - cy - mv.ty) * f;
        mv.z = nz;
    };
    if (hovered && io.MouseWheel != 0) zoomAt(std::pow(1.18f, io.MouseWheel), io.MousePos);
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) zoomAt(1.6f, io.MousePos);
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3 * S)) {
        mv.dragging = true;
        mv.tx += io.MouseDelta.x;
        mv.ty += io.MouseDelta.y;
    }
    float lim = 0.5f * std::max(size.x, size.y) * mv.z;
    mv.tx = clampf(mv.tx, -lim, lim);
    mv.ty = clampf(mv.ty, -lim, lim);
    if (mv.z <= 1.0001f) { mv.tx = 0; mv.ty = 0; }

    // terrain tint and contours
    const Terrain &T = C.terrain;
    if (T.ok) {
        float cw = (float)(dw / T.gw * s0 * mv.z), ch = (float)(dh / T.gh * s0 * mv.z);
        ImVec4 hi = mix(P.paper3, P.contour, 0.5f);
        for (int gy = 0; gy < T.gh; gy++) {
            float y0 = Y(C.maxy - (double)gy / T.gh * dh);
            if (y0 > p1.y || y0 + ch < p0.y) continue;
            for (int gx = 0; gx < T.gw; gx++) {
                float x0 = X(C.minx + (double)gx / T.gw * dw);
                if (x0 > p1.x || x0 + cw < p0.x) continue;
                float t = (T.V[(size_t)(gy * T.gw + gx)] - T.zmin) / std::max(1.f, T.zmax - T.zmin);
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cw + 0.7f, y0 + ch + 0.7f), col(mix(P.paper, hi, 0.08f + 0.55f * t)));
            }
        }
        for (const auto &lev : T.levels) {
            ImU32 cc = col(P.contour, lev.major ? 0.95f : 0.6f);
            float th = (lev.major ? 1.2f : 0.7f) * S;
            for (size_t k = 0; k + 3 < lev.segs.size(); k += 4)
                dl->AddLine(ImVec2(X(lev.segs[k]), Y(lev.segs[k + 1])), ImVec2(X(lev.segs[k + 2]), Y(lev.segs[k + 3])), cc, th);
        }
    }

    // roads (planner traffic layer: width and colour by planned trips)
    bool traffic = mode == MAP_PLANNER && app.traffic && R;
    float zk = std::pow(mv.z, 0.35f);
    for (int e = 0; e < sc.ne; e++) {
        const ev_node &a = sc.nodes[sc.edges[e].u], &b = sc.nodes[sc.edges[e].v];
        ImVec2 A(X(a.x), Y(a.y)), B(X(b.x), Y(b.y));
        if (traffic) {
            float r = (float)R->edge_load[(size_t)e] / (float)R->edge_load_max;
            ImVec4 c = r <= 0 ? P.ink3 : (r < 0.5f ? mix(P.cobaltSoft, P.cobalt, r * 2) : mix(P.cobalt, P.red, (r - 0.5f) * 2));
            dl->AddLine(A, B, col(c, r <= 0 ? 0.25f : 0.95f), (1.0f + 6.0f * r) * S * zk);
        } else {
            dl->AddLine(A, B, col(P.ink3, 0.55f), 1.15f * S * zk);
        }
    }
    float jr = 1.6f * S * zk;
    if (mv.z > 2.2f)
        for (int i = 0; i < sc.nn; i++) dl->AddCircleFilled(ImVec2(X(sc.nodes[i].x), Y(sc.nodes[i].y)), jr, col(P.ink3, 0.7f));

    // hover target: nearest station within reach
    mv.hover = -1;
    if (hovered && !mv.dragging) {
        float best = (12 * S) * (12 * S);
        for (int i = 0; i < sc.ns; i++) {
            if (mode == MAP_LIVE && (!R || !R->built[(size_t)i])) continue;
            const ev_node &n = sc.nodes[sc.stations[i].node];
            float dx = X(n.x) - io.MousePos.x, dy = Y(n.y) - io.MousePos.y;
            if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; mv.hover = i; }
        }
    }

    if (mode == MAP_PLANNER) {
        // replayed trip
        double now = ImGui::GetTime();
        if (app.trip >= 0) {
            const TripDetail &D = E.trip(app.trip);
            if (D.valid && D.path.size() >= 2) {
                std::vector<ImVec2> pts;
                for (int v : D.path) pts.push_back(ImVec2(X(sc.nodes[v].x), Y(sc.nodes[v].y)));
                float prog = (float)std::min(1.0, (now - app.trip_anim) / 2.2);
                ImU32 pc = D.plan.feasible ? col(P.cobalt) : col(P.red);
                dl->AddPolyline(pts.data(), (int)pts.size(), D.plan.feasible ? col(P.cobalt, 0.25f) : col(P.red, 0.25f), 0, 5 * S);
                int upto = std::max(1, (int)std::floor(prog * (pts.size() - 1)));
                std::vector<ImVec2> done(pts.begin(), pts.begin() + upto + 1);
                if (upto < (int)pts.size() - 1) {
                    float f = prog * (pts.size() - 1) - upto;
                    ImVec2 a = pts[(size_t)upto], b = pts[(size_t)upto + 1];
                    done.back() = a;
                    done.push_back(ImVec2(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f));
                }
                dl->AddPolyline(done.data(), (int)done.size(), pc, 0, 3.4f * S);
                dl->AddCircleFilled(pts.front(), 6 * S, col(P.paper));
                dl->AddCircle(pts.front(), 6 * S, pc, 0, 2.5f * S);
                dl->AddCircleFilled(pts.back(), 6 * S, col(P.ink));
                dl->AddCircle(pts.back(), 6 * S, col(P.paper), 0, 1.5f * S);
                for (int k = 0; k < D.plan.n_stops; k++) {
                    const ev_node &n = sc.nodes[sc.stations[D.plan.stop[k]].node];
                    ImVec2 c(X(n.x) - 12 * S, Y(n.y) - 12 * S);
                    dl->AddCircleFilled(c, 8 * S, col(P.ink));
                    std::string num = std::to_string(k + 1);
                    ImVec2 ts = ImGui::CalcTextSize(num.c_str());
                    dl->AddText(app.fonts.small, app.fonts.small->FontSize, ImVec2(c.x - ts.x * 0.42f, c.y - app.fonts.small->FontSize / 2),
                                col(P.paper), num.c_str());
                }
            }
        }
        // stations
        for (int i = 0; i < sc.ns; i++) {
            const ev_node &n = sc.nodes[sc.stations[i].node];
            ImVec2 c(X(n.x), Y(n.y));
            bool built = R && R->built[(size_t)i];
            bool lock = E.plan.edits.lock.count(i) > 0, ban = E.plan.edits.ban.count(i) > 0;
            bool portEdit = E.plan.edits.ports.count(i) > 0;
            int ports = E.effectivePorts(i);
            if (built) {
                float r = (3.8f + std::sqrt((float)ports) * 1.5f) * S * std::pow(mv.z, 0.2f);
                dl->AddCircleFilled(c, r + 4 * S, col(P.cobalt, 0.16f));
                dl->AddCircleFilled(c, r, col(P.cobalt));
                if (R->short_of[(size_t)i] > 0) dl->AddCircle(c, r + 2.5f * S, col(P.amber), 0, 2.2f * S);
            } else {
                dl->AddCircleFilled(c, 3.6f * S, col(P.paper));
                dl->AddCircle(c, 3.6f * S, col(P.ink3), 0, 1.3f * S);
            }
            if (lock) {
                float h = 9 * S;
                dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col(P.ink), 2 * S, 0, 1.6f * S);
            }
            if (ban) {
                float h = 6 * S;
                dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col(P.red), 2.2f * S);
                dl->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), col(P.red), 2.2f * S);
            }
            if (portEdit) {
                std::string lab = std::to_string(ports) + "p";
                dl->AddText(app.fonts.small, app.fonts.small->FontSize, ImVec2(c.x + 9 * S, c.y - 20 * S), col(P.ink), lab.c_str());
            }
            if (i == app.panel || i == mv.hover) dl->AddCircle(c, 13 * S, col(P.ink, i == app.panel ? 1 : 0.5f), 0, 2 * S);
        }
    } else if (live && sim) {
        // station occupancy: ring of ports, arc of ports in use, badge of drivers waiting
        for (int k = 0; k < sim->sim.nst; k++) {
            int s = sim->sim.st[k].station;
            const ev_node &n = sc.nodes[sc.stations[s].node];
            ImVec2 c(X(n.x), Y(n.y));
            int ports = R ? R->ports[(size_t)s] : sc.stations[s].ports;
            float r = 7.5f * S * std::pow(mv.z, 0.2f);
            int busy = live->charging[(size_t)k], wait = live->waiting[(size_t)k];
            dl->AddCircleFilled(c, r, col(P.paper));
            dl->AddCircle(c, r, col(P.ink2), 0, 1.4f * S);
            if (busy > 0) {
                float frac = std::min(1.f, (float)busy / std::max(1, ports));
                dl->PathArcTo(c, r + 3.2f * S, -3.14159265f / 2, -3.14159265f / 2 + 2 * 3.14159265f * frac, 32);
                dl->PathStroke(col(frac >= 1 ? P.amber : P.cobalt), 0, 3.2f * S);
            }
            if (wait > 0) {
                ImVec2 b(c.x + r + 6 * S, c.y - r - 4 * S);
                std::string w = std::to_string(wait);
                ImVec2 ts = app.fonts.small->CalcTextSizeA(app.fonts.small->FontSize, 1e9f, 0, w.c_str());
                float bw = std::max(ts.x + 8 * S, 16 * S);
                dl->AddRectFilled(ImVec2(b.x - bw / 2, b.y - 8 * S), ImVec2(b.x + bw / 2, b.y + 8 * S), col(P.amber), 8 * S);
                dl->AddText(app.fonts.small, app.fonts.small->FontSize, ImVec2(b.x - ts.x / 2, b.y - ts.y / 2), col(P.paper), w.c_str());
            }
            if (s == mv.hover) dl->AddCircle(c, r + 8 * S, col(P.ink, 0.5f), 0, 1.5f * S);
        }
        // vehicles
        for (int v = 0; v < live->ncars; v++) {
            float x = live->cars[(size_t)v * 3], y = live->cars[(size_t)v * 3 + 1], b = live->cars[(size_t)v * 3 + 2];
            dl->AddCircleFilled(ImVec2(X(x), Y(y)), 2.6f * S, col(b < 0.3f ? P.amber : P.cobalt, 0.9f));
        }
    }
    dl->PopClipRect();
    dl->AddRect(p0, p1, col(P.line), 10 * S);

    // click (not a drag) on a station
    if (ImGui::IsItemDeactivated()) {
        if (!mv.dragging && mv.hover >= 0) picked = mv.hover;
        mv.dragging = false;
    }

    // tooltip
    if (mv.hover >= 0 && hovered) {
        int i = mv.hover;
        const ev_station &st = sc.stations[i];
        ImGui::BeginTooltip();
        ImGui::PushFont(app.fonts.semi);
        ImGui::Text("Station %d", st.id);
        ImGui::PopFont();
        ImGui::Text("Junction %s", sc.nodes[st.node].name);
        ImGui::Text("%d ports at %g kW, %s", E.effectivePorts(i), st.power_kw, rupees(st.cost).c_str());
        if (mode == MAP_PLANNER) {
            bool built = R && R->built[(size_t)i];
            ImGui::TextDisabled("%s. Click to edit.", built ? "Built" : "Not built");
        } else if (live && sim) {
            auto it = sim->local.find(i);
            if (it != sim->local.end()) {
                int k = it->second;
                ImGui::Text("%d of %d ports busy, %d waiting", live->charging[(size_t)k], E.effectivePorts(i), live->waiting[(size_t)k]);
            }
        }
        ImGui::EndTooltip();
    }

    // overlay: zoom controls (and the traffic layer on the planner)
    ImVec2 keep = ImGui::GetCursorScreenPos();
    float bh = ImGui::GetFrameHeight();
    float bwid = 34 * S;
    ImGui::SetCursorScreenPos(ImVec2(p1.x - 10 * S - bwid * 3 - 2 * S * 2, p0.y + 10 * S));
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2 * S, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, P.paper);
    if (ImGui::Button("+", ImVec2(bwid, bh))) zoomAt(1.6f, ImVec2(cx, cy));
    ImGui::SameLine();
    if (ImGui::Button("\xE2\x88\x92", ImVec2(bwid, bh))) zoomAt(1 / 1.6f, ImVec2(cx, cy));
    ImGui::SameLine();
    if (ImGui::Button("Fit", ImVec2(bwid, bh))) mv.reset();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (mode == MAP_PLANNER) {
        float tw = ImGui::CalcTextSize("Traffic").x + ImGui::GetFrameHeight() + 18 * S;
        ImGui::SetCursorScreenPos(ImVec2(p1.x - 10 * S - bwid * 3 - 4 * S - 10 * S - tw, p0.y + 10 * S));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, P.paper);
        ImGui::Checkbox("Traffic", &app.traffic);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Colour each road by how many planned trips use it");
    }
    ImGui::PopID();

    // legend
    {
        ImFont *f = app.fonts.small;
        float fs = f->FontSize;
        struct Item { int kind; const char *label; };
        static const Item PLAN[] = { { 0, "Road" }, { 1, "Candidate site" }, { 2, "Built station" }, { 3, "Short of chargers" }, { 4, "Contour, 50 m" } };
        static const Item LIVE[] = { { 5, "Vehicle" }, { 6, "Battery under 30%" }, { 7, "Ports in use" }, { 8, "Drivers waiting" } };
        static const Item BUSY = { 9, "Busier road" };
        const Item *list = mode == MAP_PLANNER ? PLAN : LIVE;
        int count = mode == MAP_PLANNER ? 5 : 4;
        auto item = [&](int k) -> const Item & { return (k == 0 && mode == MAP_PLANNER && app.traffic) ? BUSY : list[k]; };
        float x = p0.x + 12 * S, y = p1.y - 12 * S - fs - 8 * S;
        float total = 0;
        for (int k = 0; k < count; k++) total += 22 * S + f->CalcTextSizeA(fs, 1e9f, 0, item(k).label).x + 14 * S;
        dl->AddRectFilled(ImVec2(x - 6 * S, y - 6 * S), ImVec2(x + total, y + fs + 8 * S), col(P.paper, 0.9f), 6 * S);
        for (int k = 0; k < count; k++) {
            const Item &it = item(k);
            ImVec2 c(x + 8 * S, y + fs / 2 + 1);
            switch (it.kind) {
            case 0: dl->AddLine(ImVec2(c.x - 7 * S, c.y), ImVec2(c.x + 7 * S, c.y), col(P.ink3), 1.5f * S); break;
            case 1: dl->AddCircle(c, 4 * S, col(P.ink3), 0, 1.3f * S); break;
            case 2: dl->AddCircleFilled(c, 5 * S, col(P.cobalt)); break;
            case 3: dl->AddCircleFilled(c, 4.5f * S, col(P.cobalt)); dl->AddCircle(c, 7 * S, col(P.amber), 0, 2 * S); break;
            case 4: dl->AddLine(ImVec2(c.x - 7 * S, c.y), ImVec2(c.x + 7 * S, c.y), col(P.contour), 1.2f * S); break;
            case 5: dl->AddCircleFilled(c, 3 * S, col(P.cobalt)); break;
            case 6: dl->AddCircleFilled(c, 3 * S, col(P.amber)); break;
            case 7: dl->PathArcTo(c, 6 * S, -3.14159265f / 2, 3.14159265f, 16); dl->PathStroke(col(P.cobalt), 0, 2.4f * S); break;
            case 8: dl->AddRectFilled(ImVec2(c.x - 7 * S, c.y - 7 * S), ImVec2(c.x + 7 * S, c.y + 7 * S), col(P.amber), 7 * S); break;
            case 9: dl->AddLine(ImVec2(c.x - 7 * S, c.y), ImVec2(c.x + 7 * S, c.y), col(P.red), 4 * S); break;
            }
            dl->AddText(f, fs, ImVec2(x + 20 * S, y), col(P.ink2), it.label);
            x += 22 * S + f->CalcTextSizeA(fs, 1e9f, 0, it.label).x + 14 * S;
        }
    }
    ImGui::SetCursorScreenPos(keep);
    return picked;
}

} // namespace ev33
