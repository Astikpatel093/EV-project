// network.cpp - the road-network canvas: terrain, roads, junctions, stations, a route and the day
// simulation, drawn with ImDrawList.
//
// Positions come from Scene::nodes (x, y in km): the engine projects each node's latitude and
// longitude onto a flat plane around the network's mean latitude (equirectangular projection),
// so distances on screen are proportional to distances on the ground.
//
// Markers:  junction = small grey dot      existing station = blue square
//           offline station = square with a red outline and cross
//           candidate site = hollow ring    new (selected) station = green diamond
//           route = green line, start = ring, end = filled dot, charging stops numbered
#include <algorithm>
#include <cmath>
#include <string>

#include "ui.h"

namespace ev33 {

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void diamond(ImDrawList *dl, ImVec2 c, float r, ImU32 fill) {
    dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), fill);
}

int DrawNetwork(App &app, NetView &v, const char *id, ImVec2 size, NetMode mode, const NetLayers &L, const std::vector<int> *route,
                const std::vector<int> *stops, const LiveState *live, const SimView *sim) {
    Engine &E = app.eng;
    City &C = E.city();
    const Scene &sc = C.sc;
    const Palette &P = app.pal;
    Run *R = E.run();
    int picked = -1;
    float S = app.scale;

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, col(P.panel), 4 * S);
    dl->PushClipRect(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p1.y - 1), true);

    // data (km) -> screen
    double dw = std::max(1e-6, C.maxx - C.minx), dh = std::max(1e-6, C.maxy - C.miny);
    float pad = 22 * S;
    float s0 = (float)std::min((size.x - 2 * pad) / dw, (size.y - 2 * pad) / dh);
    float cx = (p0.x + p1.x) / 2, cy = (p0.y + p1.y) / 2;
    double mx = (C.minx + C.maxx) / 2, my = (C.miny + C.maxy) / 2;
    auto X = [&](double x) { return cx + (float)((x - mx) * s0 * v.z) + v.tx; };
    auto Y = [&](double y) { return cy - (float)((y - my) * s0 * v.z) + v.ty; };

    // wheel zooms at the pointer, drag pans, double-click zooms in
    ImGuiIO &io = ImGui::GetIO();
    auto zoomAt = [&](float f, ImVec2 at) {
        float nz = clampf(v.z * f, 1.f, 24.f);
        f = nz / v.z;
        v.tx = at.x - cx - (at.x - cx - v.tx) * f;
        v.ty = at.y - cy - (at.y - cy - v.ty) * f;
        v.z = nz;
    };
    if (hovered && io.MouseWheel != 0) zoomAt(std::pow(1.18f, io.MouseWheel), io.MousePos);
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) zoomAt(1.6f, io.MousePos);
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3 * S)) {
        v.dragging = true;
        v.tx += io.MouseDelta.x;
        v.ty += io.MouseDelta.y;
    }
    float lim = 0.5f * std::max(size.x, size.y) * v.z;
    v.tx = clampf(v.tx, -lim, lim);
    v.ty = clampf(v.ty, -lim, lim);
    if (v.z <= 1.0001f) { v.tx = 0; v.ty = 0; }

    // terrain shading and 50 m contours
    const Terrain &T = C.terrain;
    if (L.terrain && T.ok) {
        float cw = (float)(dw / T.gw * s0 * v.z), ch = (float)(dh / T.gh * s0 * v.z);
        for (int gy = 0; gy < T.gh; gy++) {
            float y0 = Y(C.maxy - (double)gy / T.gh * dh);
            if (y0 > p1.y || y0 + ch < p0.y) continue;
            for (int gx = 0; gx < T.gw; gx++) {
                float x0 = X(C.minx + (double)gx / T.gw * dw);
                if (x0 > p1.x || x0 + cw < p0.x) continue;
                float t = (T.V[(size_t)(gy * T.gw + gx)] - T.zmin) / std::max(1.f, T.zmax - T.zmin);
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cw + 0.7f, y0 + ch + 0.7f), col(mix(P.panel, P.contour, 0.05f + 0.45f * t)));
            }
        }
        for (const auto &lev : T.levels) {
            ImU32 cc = col(P.contour, lev.major ? 0.95f : 0.55f);
            float th = (lev.major ? 1.1f : 0.6f) * S;
            for (size_t k = 0; k + 3 < lev.segs.size(); k += 4)
                dl->AddLine(ImVec2(X(lev.segs[k]), Y(lev.segs[k + 1])), ImVec2(X(lev.segs[k + 2]), Y(lev.segs[k + 3])), cc, th);
        }
    }

    // roads (optionally coloured by planned trips)
    bool traffic = mode == NET_PLAN && L.traffic && R;
    float zk = std::pow(v.z, 0.35f);
    bool big = sc.nn > 600;   // simplified drawing for large networks until zoomed in
    for (int e = 0; e < sc.ne; e++) {
        const NodeV &a = sc.nodes[(size_t)sc.edges[(size_t)e].u], &b = sc.nodes[(size_t)sc.edges[(size_t)e].v];
        ImVec2 A(X(a.x), Y(a.y)), B(X(b.x), Y(b.y));
        if ((A.x < p0.x && B.x < p0.x) || (A.x > p1.x && B.x > p1.x) || (A.y < p0.y && B.y < p0.y) || (A.y > p1.y && B.y > p1.y)) continue;
        if (traffic) {
            float r = (float)R->edge_load[(size_t)e] / (float)R->edge_load_max;
            ImVec4 c = r <= 0 ? P.ink3 : (r < 0.5f ? mix(P.secondarySoft, P.secondary, r * 2) : mix(P.secondary, P.error, (r - 0.5f) * 2));
            dl->AddLine(A, B, col(c, r <= 0 ? 0.3f : 0.95f), (1.0f + 5.0f * r) * S * zk);
        } else {
            dl->AddLine(A, B, col(P.ink3, big && v.z < 2 ? 0.45f : 0.6f), (big && v.z < 2 ? 0.9f : 1.15f) * S * zk);
        }
    }
    if (L.junctions && (!big || v.z > 2.0f)) {
        float jr = 1.5f * S * zk;
        for (const NodeV &n : sc.nodes) {
            float x = X(n.x), y = Y(n.y);
            if (x < p0.x || x > p1.x || y < p0.y || y > p1.y) continue;
            dl->AddCircleFilled(ImVec2(x, y), jr, col(P.ink3, 0.75f));
        }
    }
    if (L.labels && v.z > 3.0f) {
        ImFont *f = app.fonts.small;
        for (const NodeV &n : sc.nodes) {
            float x = X(n.x), y = Y(n.y);
            if (x < p0.x || x > p1.x || y < p0.y || y > p1.y) continue;
            dl->AddText(f, f->FontSize * 0.9f, ImVec2(x + 4 * S, y - 14 * S), col(P.ink3), n.name.c_str());
        }
    }

    // hover: nearest station within reach
    v.hover = -1;
    if (hovered && !v.dragging) {
        float best = (12 * S) * (12 * S);
        for (int i = 0; i < sc.ns; i++) {
            const SiteV &si = sc.stations[(size_t)i];
            bool shown = mode == NET_DAY ? (R && R->built[(size_t)i]) : si.existing ? L.existing : (R && R->built[(size_t)i]) ? L.built : L.candidates;
            if (!shown) continue;
            const NodeV &n = sc.nodes[(size_t)si.node];
            float dx = X(n.x) - io.MousePos.x, dy = Y(n.y) - io.MousePos.y;
            if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; v.hover = i; }
        }
    }

    // route overlay
    if (L.route && route && route->size() >= 2) {
        std::vector<ImVec2> pts;
        for (int n : *route) pts.push_back(ImVec2(X(sc.nodes[(size_t)n].x), Y(sc.nodes[(size_t)n].y)));
        bool ok = stops != nullptr;   // a feasible plan passes its stops (possibly none)
        ImVec4 rc = ok ? P.primary : P.error;
        dl->AddPolyline(pts.data(), (int)pts.size(), col(rc, 0.22f), 0, 7 * S);
        dl->AddPolyline(pts.data(), (int)pts.size(), col(rc), 0, 3.0f * S);
        dl->AddCircleFilled(pts.front(), 6 * S, col(P.panel));
        dl->AddCircle(pts.front(), 6 * S, col(rc), 0, 2.5f * S);
        dl->AddCircleFilled(pts.back(), 6 * S, col(rc));
    }

    if (mode == NET_PLAN) {
        for (int i = 0; i < sc.ns; i++) {
            const SiteV &si = sc.stations[(size_t)i];
            const NodeV &n = sc.nodes[(size_t)si.node];
            ImVec2 c(X(n.x), Y(n.y));
            if (c.x < p0.x - 20 || c.x > p1.x + 20 || c.y < p0.y - 20 || c.y > p1.y + 20) continue;
            bool built = R && R->built[(size_t)i];
            int ports = E.effectivePorts(i);
            float grow = std::pow(v.z, 0.2f);
            if (si.existing) {
                if (!L.existing) continue;
                float r = (3.2f + std::sqrt((float)ports) * 1.1f) * S * grow;
                if (si.available) {
                    dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col(P.secondary), 1 * S);
                } else {
                    dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col(P.panel), 1 * S);
                    dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col(P.error), 1 * S, 0, 1.6f * S);
                    dl->AddLine(ImVec2(c.x - r * 0.7f, c.y - r * 0.7f), ImVec2(c.x + r * 0.7f, c.y + r * 0.7f), col(P.error), 1.5f * S);
                    dl->AddLine(ImVec2(c.x - r * 0.7f, c.y + r * 0.7f), ImVec2(c.x + r * 0.7f, c.y - r * 0.7f), col(P.error), 1.5f * S);
                }
            } else if (built) {
                if (!L.built) continue;
                float r = (4.6f + std::sqrt((float)ports) * 1.3f) * S * grow;
                diamond(dl, c, r + 2 * S, col(P.panel));
                diamond(dl, c, r, col(P.primary));
            } else {
                if (!L.candidates) continue;
                dl->AddCircleFilled(c, 3.6f * S, col(P.panel));
                dl->AddCircle(c, 3.6f * S, col(P.ink2), 0, 1.3f * S);
            }
            if (built && R && R->short_of[(size_t)i] > 0) dl->AddCircle(c, 10 * S * grow, col(P.warn), 0, 2.0f * S);
            if (E.plan.edits.lock.count(i)) dl->AddRect(ImVec2(c.x - 11 * S, c.y - 11 * S), ImVec2(c.x + 11 * S, c.y + 11 * S), col(P.ink), 2 * S, 0, 1.4f * S);
            if (E.plan.edits.ban.count(i)) {
                dl->AddLine(ImVec2(c.x - 7 * S, c.y - 7 * S), ImVec2(c.x + 7 * S, c.y + 7 * S), col(P.error), 2.2f * S);
                dl->AddLine(ImVec2(c.x - 7 * S, c.y + 7 * S), ImVec2(c.x + 7 * S, c.y - 7 * S), col(P.error), 2.2f * S);
            }
            if (i == app.station_panel || i == v.hover) dl->AddCircle(c, 14 * S, col(P.ink, i == app.station_panel ? 1 : 0.5f), 0, 1.8f * S);
        }
        // numbered charging stops
        if (L.route && stops)
            for (size_t k = 0; k < stops->size(); k++) {
                const NodeV &n = sc.nodes[(size_t)sc.stations[(size_t)(*stops)[k]].node];
                ImVec2 c(X(n.x) + 13 * S, Y(n.y) - 13 * S);
                dl->AddCircleFilled(c, 8.5f * S, col(P.ink));
                std::string num = std::to_string(k + 1);
                ImFont *f = app.fonts.smallSemi;
                ImVec2 ts = f->CalcTextSizeA(f->FontSize, 1e9f, 0, num.c_str());
                dl->AddText(f, f->FontSize, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), col(P.panel), num.c_str());
            }
    } else if (live && sim) {
        // station occupancy: ring = ports, arc = ports in use, badge = drivers waiting
        for (int k = 0; k < sim->sim.nst; k++) {
            int s = sim->sim.st[(size_t)k].station;
            const NodeV &n = sc.nodes[(size_t)sc.stations[(size_t)s].node];
            ImVec2 c(X(n.x), Y(n.y));
            int ports = R ? R->ports[(size_t)s] : sc.stations[(size_t)s].ports;
            float r = 7 * S * std::pow(v.z, 0.2f);
            int busy = live->charging[(size_t)k], wait = live->waiting[(size_t)k];
            dl->AddCircleFilled(c, r, col(P.panel));
            dl->AddCircle(c, r, col(sc.stations[(size_t)s].existing ? P.secondary : P.primary), 0, 1.6f * S);
            if (busy > 0) {
                float frac = std::min(1.f, (float)busy / (float)std::max(1, ports));
                dl->PathArcTo(c, r + 3 * S, -3.14159265f / 2, -3.14159265f / 2 + 2 * 3.14159265f * frac, 32);
                dl->PathStroke(col(frac >= 1 ? P.warn : P.primary), 0, 3 * S);
            }
            if (wait > 0) {
                ImVec2 b(c.x + r + 6 * S, c.y - r - 4 * S);
                std::string w = std::to_string(wait);
                ImFont *f = app.fonts.smallSemi;
                ImVec2 ts = f->CalcTextSizeA(f->FontSize, 1e9f, 0, w.c_str());
                float bw = std::max(ts.x + 8 * S, 16 * S);
                dl->AddRectFilled(ImVec2(b.x - bw / 2, b.y - 8 * S), ImVec2(b.x + bw / 2, b.y + 8 * S), col(P.warn), 3 * S);
                dl->AddText(f, f->FontSize, ImVec2(b.x - ts.x / 2, b.y - ts.y / 2), col(P.panel), w.c_str());
            }
            if (s == v.hover) dl->AddCircle(c, r + 8 * S, col(P.ink, 0.5f), 0, 1.5f * S);
        }
        for (int k = 0; k < live->ncars; k++) {
            float x = live->cars[(size_t)k * 3], y = live->cars[(size_t)k * 3 + 1], b = live->cars[(size_t)k * 3 + 2];
            dl->AddCircleFilled(ImVec2(X(x), Y(y)), 2.5f * S, col(b < 0.3f ? P.warn : P.secondary, 0.95f));
        }
    }
    dl->PopClipRect();
    dl->AddRect(p0, p1, col(P.line), 4 * S);

    // a click that was not a drag picks the station under the pointer
    if (ImGui::IsItemDeactivated()) {
        if (!v.dragging && v.hover >= 0) picked = v.hover;
        v.dragging = false;
    }
    if (v.hover >= 0 && hovered) {
        int i = v.hover;
        const SiteV &st = sc.stations[(size_t)i];
        ImGui::BeginTooltip();
        ImGui::PushFont(app.fonts.semi);
        ImGui::Text("%s  (station %d)", st.name.c_str(), st.id);
        ImGui::PopFont();
        ImGui::Text("Junction %s", sc.nodes[(size_t)st.node].name.c_str());
        ImGui::Text("%d ports at %g kW, connector %s", E.effectivePorts(i), st.power_kw, st.connector.empty() ? "not recorded" : st.connector.c_str());
        if (st.existing) ImGui::TextDisabled("Existing station, %s", st.status.c_str());
        else ImGui::TextDisabled("Candidate site, %s, benefit %s: %s", rupees(st.cost).c_str(), fint((double)st.value).c_str(),
                                 R && R->built[(size_t)i] ? "selected" : "not selected");
        if (live && sim) {
            auto it = sim->local.find(i);
            if (it != sim->local.end())
                ImGui::Text("Now: %d of %d ports busy, %d waiting", live->charging[(size_t)it->second], E.effectivePorts(i), live->waiting[(size_t)it->second]);
        }
        ImGui::EndTooltip();
    }

    // zoom controls in the top-right corner
    ImVec2 keep = ImGui::GetCursorScreenPos();
    float bh = ImGui::GetFrameHeight(), bw = 32 * S;
    ImGui::SetCursorScreenPos(ImVec2(p1.x - 8 * S - bw * 3, p0.y + 8 * S));
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, P.panel);
    if (ImGui::Button("+", ImVec2(bw, bh))) zoomAt(1.6f, ImVec2(cx, cy));
    ImGui::SameLine();
    if (ImGui::Button("\xE2\x88\x92", ImVec2(bw, bh))) zoomAt(1 / 1.6f, ImVec2(cx, cy));
    ImGui::SameLine();
    if (ImGui::Button("Fit", ImVec2(bw, bh))) v.reset();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
    // scale bar (km) in the bottom-left corner
    {
        double kmPerPx = 1.0 / (s0 * v.z);
        double target = 90 * S * kmPerPx, step = std::pow(10.0, std::floor(std::log10(target)));
        double km = target / step >= 5 ? 5 * step : target / step >= 2 ? 2 * step : step;
        float len = (float)(km / kmPerPx);
        ImVec2 a(p0.x + 12 * S, p1.y - 14 * S);
        dl->AddLine(a, ImVec2(a.x + len, a.y), col(P.ink2), 2 * S);
        dl->AddLine(ImVec2(a.x, a.y - 4 * S), ImVec2(a.x, a.y + 1), col(P.ink2), 1.5f * S);
        dl->AddLine(ImVec2(a.x + len, a.y - 4 * S), ImVec2(a.x + len, a.y + 1), col(P.ink2), 1.5f * S);
        std::string t = (km >= 1 ? fint(km) : f1(km)) + " km";
        dl->AddText(app.fonts.small, app.fonts.small->FontSize, ImVec2(a.x + len + 6 * S, a.y - app.fonts.small->FontSize * 0.75f), col(P.ink2), t.c_str());
    }
    ImGui::SetCursorScreenPos(keep);
    return picked;
}

void NetworkLegend(App &app, NetMode mode, const NetLayers &L) {
    const Palette &P = app.pal;
    float S = app.scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImFont *f = app.fonts.small;
    float fs = f->FontSize;
    auto row = [&](int kind, const char *label) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 c(p.x + 9 * S, p.y + fs / 2 + 2 * S);
        switch (kind) {
        case 0: dl->AddLine(ImVec2(c.x - 8 * S, c.y), ImVec2(c.x + 8 * S, c.y), col(P.ink3), 1.5f * S); break;
        case 1: dl->AddCircleFilled(c, 2 * S, col(P.ink3)); break;
        case 2: dl->AddRectFilled(ImVec2(c.x - 5 * S, c.y - 5 * S), ImVec2(c.x + 5 * S, c.y + 5 * S), col(P.secondary), 1 * S); break;
        case 3:
            dl->AddRect(ImVec2(c.x - 5 * S, c.y - 5 * S), ImVec2(c.x + 5 * S, c.y + 5 * S), col(P.error), 1 * S, 0, 1.5f * S);
            dl->AddLine(ImVec2(c.x - 3 * S, c.y - 3 * S), ImVec2(c.x + 3 * S, c.y + 3 * S), col(P.error), 1.4f * S);
            break;
        case 4: dl->AddCircle(c, 3.6f * S, col(P.ink2), 0, 1.3f * S); break;
        case 5: diamond(dl, c, 6 * S, col(P.primary)); break;
        case 6: dl->AddLine(ImVec2(c.x - 8 * S, c.y), ImVec2(c.x + 8 * S, c.y), col(P.primary), 3 * S); break;
        case 7: dl->AddCircle(c, 6 * S, col(P.warn), 0, 2 * S); break;
        case 8: dl->AddLine(ImVec2(c.x - 8 * S, c.y), ImVec2(c.x + 8 * S, c.y), col(P.contour), 1.2f * S); break;
        case 9: dl->AddLine(ImVec2(c.x - 8 * S, c.y), ImVec2(c.x + 8 * S, c.y), col(P.error), 4 * S); break;
        case 10: dl->AddCircleFilled(c, 2.6f * S, col(P.secondary)); break;
        case 11: dl->AddCircleFilled(c, 2.6f * S, col(P.warn)); break;
        case 12: dl->PathArcTo(c, 5.5f * S, -3.14159265f / 2, 3.14159265f, 16); dl->PathStroke(col(P.primary), 0, 2.4f * S); break;
        case 13: dl->AddRectFilled(ImVec2(c.x - 7 * S, c.y - 6 * S), ImVec2(c.x + 7 * S, c.y + 6 * S), col(P.warn), 3 * S); break;
        }
        ImGui::Dummy(ImVec2(22 * S, fs));
        ImGui::SameLine();
        ImGui::PushFont(f);
        ImGui::TextColored(P.ink2, "%s", label);
        ImGui::PopFont();
    };
    if (mode == NET_PLAN) {
        row(L.traffic ? 9 : 0, L.traffic ? "Road (wider, redder = more planned trips)" : "Road");
        row(1, "Junction");
        row(2, "Existing station");
        row(3, "Existing station, offline");
        row(4, "Candidate site");
        row(5, "New station (selected)");
        row(6, "Calculated route");
        row(7, "Station short of chargers");
        row(8, "Contour, every 50 m");
    } else {
        row(10, "Vehicle on the road");
        row(11, "Vehicle below 30% battery");
        row(12, "Ports in use (full ring = all busy)");
        row(13, "Drivers waiting");
    }
}

} // namespace ev33
