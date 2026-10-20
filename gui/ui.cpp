// ui.cpp - theme, fonts, formatting, rich text and PNG export for the EV33 desktop app.
#include "ui.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "implot.h"

namespace ev33 {

const char *const PAGE_NAMES[PAGE_COUNT] = { "Planner", "Live day", "Strategies", "Capacity", "Algorithms", "Data model", "Benchmarks" };

/* ======================================================================== colours */
static ImVec4 hex(unsigned v, float a = 1) {
    return ImVec4(((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, a);
}
ImU32 col(const ImVec4 &c, float alpha) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }
ImVec4 mix(const ImVec4 &a, const ImVec4 &b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

Palette makePalette(bool dark) {
    Palette p;
    if (!dark) {
        p.paper = hex(0xF2F4F0); p.paper2 = hex(0xE6EBE3); p.paper3 = hex(0xD9E0D5);
        p.ink = hex(0x15233B); p.ink2 = hex(0x4A5970); p.ink3 = hex(0x7A879A);
        p.line = hex(0xC6D0C3); p.contour = hex(0xA7B7A2);
        p.cobalt = hex(0x2C55E8); p.cobaltSoft = hex(0xD9E1FB);
        p.amber = hex(0xB97710); p.amberSoft = hex(0xF4E3C2);
        p.red = hex(0xC2433A); p.green = hex(0x2D7D53); p.greenSoft = hex(0xD6EBDD);
    } else {
        p.paper = hex(0x0E1722); p.paper2 = hex(0x132030); p.paper3 = hex(0x1B2C40);
        p.ink = hex(0xE5EBF1); p.ink2 = hex(0xA3B0C0); p.ink3 = hex(0x74839A);
        p.line = hex(0x26384C); p.contour = hex(0x35554A);
        p.cobalt = hex(0x7E9CFF); p.cobaltSoft = hex(0x1E2D57);
        p.amber = hex(0xF0B54D); p.amberSoft = hex(0x3A2C12);
        p.red = hex(0xF2786C); p.green = hex(0x62C792); p.greenSoft = hex(0x15362A);
    }
    return p;
}

void applyTheme(const Palette &p, bool dark, float scale) {
    ImGuiStyle st;
    if (dark) ImGui::StyleColorsDark(&st); else ImGui::StyleColorsLight(&st);
    st.WindowPadding = ImVec2(16, 14);
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(10, 8);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.CellPadding = ImVec2(10, 6);
    st.ScrollbarSize = 12;
    st.GrabMinSize = 12;
    st.WindowRounding = 0;
    st.ChildRounding = 10;
    st.FrameRounding = 6;
    st.PopupRounding = 8;
    st.GrabRounding = 6;
    st.TabRounding = 6;
    st.ScrollbarRounding = 6;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.FrameBorderSize = 1;
    st.PopupBorderSize = 1;
    st.TabBorderSize = 0;
    st.SeparatorTextBorderSize = 1;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = p.ink;
    c[ImGuiCol_TextDisabled] = p.ink3;
    c[ImGuiCol_WindowBg] = p.paper;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = p.paper;
    c[ImGuiCol_Border] = p.line;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = p.paper;
    c[ImGuiCol_FrameBgHovered] = p.paper2;
    c[ImGuiCol_FrameBgActive] = p.paper3;
    c[ImGuiCol_TitleBg] = p.paper2;
    c[ImGuiCol_TitleBgActive] = p.paper2;
    c[ImGuiCol_MenuBarBg] = p.paper2;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.paper3;
    c[ImGuiCol_ScrollbarGrabHovered] = p.line;
    c[ImGuiCol_ScrollbarGrabActive] = p.ink3;
    c[ImGuiCol_CheckMark] = p.cobalt;
    c[ImGuiCol_SliderGrab] = p.cobalt;
    c[ImGuiCol_SliderGrabActive] = p.cobalt;
    c[ImGuiCol_Button] = p.paper2;
    c[ImGuiCol_ButtonHovered] = p.paper3;
    c[ImGuiCol_ButtonActive] = p.cobaltSoft;
    c[ImGuiCol_Header] = p.cobaltSoft;
    c[ImGuiCol_HeaderHovered] = p.paper2;
    c[ImGuiCol_HeaderActive] = p.cobaltSoft;
    c[ImGuiCol_Separator] = p.line;
    c[ImGuiCol_SeparatorHovered] = p.ink3;
    c[ImGuiCol_SeparatorActive] = p.cobalt;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = p.paper;
    c[ImGuiCol_TabHovered] = p.paper2;
    c[ImGuiCol_TabSelected] = p.paper2;
    c[ImGuiCol_TabSelectedOverline] = p.cobalt;
    c[ImGuiCol_TabDimmed] = p.paper;
    c[ImGuiCol_TabDimmedSelected] = p.paper2;
    c[ImGuiCol_TableHeaderBg] = p.paper2;
    c[ImGuiCol_TableBorderStrong] = p.line;
    c[ImGuiCol_TableBorderLight] = p.line;
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(p.paper2.x, p.paper2.y, p.paper2.z, 0.55f);
    c[ImGuiCol_TextSelectedBg] = p.cobaltSoft;
    c[ImGuiCol_NavHighlight] = p.cobalt;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.3f);
    st.ScaleAllSizes(scale);
    ImGui::GetStyle() = st;

    ImPlotStyle &ps = ImPlot::GetStyle();
    if (dark) ImPlot::StyleColorsDark(&ps); else ImPlot::StyleColorsLight(&ps);
    ps.Colors[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0);
    ps.Colors[ImPlotCol_PlotBg] = p.paper;
    ps.Colors[ImPlotCol_PlotBorder] = p.line;
    ps.Colors[ImPlotCol_LegendBg] = p.paper;
    ps.Colors[ImPlotCol_LegendBorder] = p.line;
    ps.Colors[ImPlotCol_LegendText] = p.ink;
    ps.Colors[ImPlotCol_TitleText] = p.ink;
    ps.Colors[ImPlotCol_InlayText] = p.ink2;
    ps.Colors[ImPlotCol_AxisText] = p.ink2;
    ps.Colors[ImPlotCol_AxisGrid] = ImVec4(p.line.x, p.line.y, p.line.z, 0.8f);
    ps.Colors[ImPlotCol_AxisTick] = p.line;
    ps.Colors[ImPlotCol_Crosshairs] = p.ink3;
    ps.LineWeight = 2.0f;
    ps.PlotPadding = ImVec2(14, 10);
    ps.FitPadding = ImVec2(0.04f, 0.08f);
    ps.LegendPadding = ImVec2(8, 6);
    ps.MinorAlpha = 0.4f;
}

/* ======================================================================== fonts */
bool loadFonts(Fonts &f, const std::string &dir, float scale) {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, 0x0370, 0x03FF, 0x2000, 0x206F, 0x2070, 0x209F, 0x20A0, 0x20CF,
        0x2190, 0x21FF, 0x2200, 0x22FF, 0,
    };
    auto load = [&](const char *file, float px) -> ImFont * {
        std::string path = dir + "/" + file;
        FILE *t = std::fopen(path.c_str(), "rb");
        if (!t) return nullptr;
        std::fclose(t);
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), px * scale, &cfg, ranges);
    };
    f.body = load("IBMPlexSans-Regular.ttf", 16);
    if (!f.body) {   // fonts missing: fall back to Dear ImGui's built-in font everywhere
        ImFontConfig cfg;
        cfg.SizePixels = 13 * scale;
        f.body = io.Fonts->AddFontDefault(&cfg);
        f.bold = f.semi = f.small = f.smallBold = f.h1 = f.h2 = f.big = f.mono = f.body;
        return false;
    }
    f.semi = load("IBMPlexSans-SemiBold.ttf", 16);
    f.bold = load("IBMPlexSans-Bold.ttf", 16);
    f.small = load("IBMPlexSans-Regular.ttf", 13.5f);
    f.smallBold = load("IBMPlexSans-SemiBold.ttf", 13.5f);
    f.h2 = load("IBMPlexSans-SemiBold.ttf", 21);
    f.h1 = load("IBMPlexSans-Bold.ttf", 28);
    f.big = load("IBMPlexSans-Bold.ttf", 46);
    f.mono = load("IBMPlexMono-Regular.ttf", 14);
    if (!f.semi) f.semi = f.body;
    if (!f.bold) f.bold = f.semi;
    if (!f.mono) f.mono = f.body;
    if (!f.smallBold) f.smallBold = f.small;
    return true;
}

/* ======================================================================== text helpers */
void RichText(const std::string &s, const Fonts &f, ImU32 color, float max_width) {
    float wrap = ImGui::GetContentRegionAvail().x - 2;
    if (max_width > 0 && max_width < wrap) wrap = max_width;
    ImGui::PushTextWrapPos(-1.0f);   // words are placed by hand; ImGui must not wrap inside them
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * ImGui::GetStyle().ItemSpacing.y / 8 + 1));
    ImGui::BeginGroup();
    bool bold = false, first = true;
    float x = 0;
    ImFont *base = ImGui::GetFont();
    ImFont *boldF = base == f.small ? f.smallBold : f.bold;
    float size = ImGui::GetFontSize();
    float space = base->CalcTextSizeA(size, 1e9f, 0, " ").x;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\n') { first = true; x = 0; i++; continue; }
        if (s[i] == ' ') { i++; continue; }
        size_t j = i;
        std::string word;
        // a word ends at a space or newline; ** toggles bold and may sit inside a word
        std::vector<std::pair<std::string, bool>> parts;
        bool b = bold;
        while (j < s.size() && s[j] != ' ' && s[j] != '\n') {
            if (s.compare(j, 2, "**") == 0) { if (!word.empty()) parts.push_back({ word, b }); word.clear(); b = !b; j += 2; continue; }
            word += s[j++];
        }
        if (!word.empty()) parts.push_back({ word, b });
        bold = b;
        i = j;
        float w = 0;
        for (auto &pt : parts) w += std::ceil((pt.second ? boldF : base)->CalcTextSizeA(size, 1e9f, 0, pt.first.c_str()).x);  // ImGui rounds item widths up
        if (!first) {
            if (x + space + w > wrap) x = 0;
            else { ImGui::SameLine(0, space); x += space; }
        }
        bool firstPart = true;
        for (auto &pt : parts) {
            if (!firstPart) ImGui::SameLine(0, 0);
            ImFont *fo = pt.second ? boldF : base;
            ImGui::PushFont(fo);
            if (color) ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(pt.first.c_str());
            if (color) ImGui::PopStyleColor();
            ImGui::PopFont();
            firstPart = false;
        }
        x += w;
        first = false;
    }
    ImGui::EndGroup();
    ImGui::PopStyleVar();
    ImGui::PopTextWrapPos();
}

void Heading(const char *title, const Fonts &f, const char *sub) {
    ImGui::PushFont(f.h2);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (sub) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(ImGui::GetContentRegionAvail().x, 760.f * ImGui::GetFontSize() / 16));
        ImGui::TextWrapped("%s", sub);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

void Caption(const char *text, const Palette &p) {
    ImGui::PushStyleColor(ImGuiCol_Text, p.ink3);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void Pill(const char *text, const ImVec4 &bg, const ImVec4 &fg) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 pad(8, 2);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ts.x + pad.x * 2, ts.y + pad.y * 2);
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), col(bg), sz.y / 2);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + pad.x, p.y + pad.y), col(fg), text);
    ImGui::Dummy(sz);
}

bool Segmented(const char *id, const char *const *labels, int n, int *sel, const Palette &p, float width, const bool *disabled) {
    ImGui::PushID(id);
    float avail = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    float gap = 2;
    float w = (avail - gap * (n - 1)) / n;
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    for (int i = 0; i < n; i++) {
        bool on = *sel == i;
        bool off = disabled && disabled[i];
        ImGui::PushID(i);
        if (i) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, on ? p.ink : p.paper);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? p.ink : p.paper2);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? p.ink : p.paper3);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? p.paper : p.ink);
        ImGui::BeginDisabled(off);
        if (ImGui::Button(labels[i], ImVec2(w, 0)) && !on) { *sel = i; changed = true; }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(4);
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    // rounded outline around the whole group
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    float h = mx.y - mn.y;
    ImVec2 a(mx.x - avail, mx.y - h);
    ImGui::GetWindowDrawList()->AddRect(a, mx, col(p.line), 6, 0, 1);
    ImGui::PopID();
    return changed;
}

bool Chips(const char *id, const char *const *labels, int n, int *sel, const Palette &p) {
    ImGui::PushID(id);
    bool changed = false;
    float avail = ImGui::GetContentRegionAvail().x, x = 0, sp = ImGui::GetStyle().ItemSpacing.x;
    for (int i = 0; i < n; i++) {
        bool on = *sel == i;
        float w = ImGui::CalcTextSize(labels[i]).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i) { if (x + sp + w > avail) x = 0; else { ImGui::SameLine(); x += sp; } }
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? p.ink : p.paper);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? p.ink : p.paper2);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? p.ink : p.paper3);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? p.paper : p.ink);
        ImGui::PushStyleColor(ImGuiCol_Border, on ? p.ink : p.line);
        if (ImGui::Button(labels[i]) && !on) { *sel = i; changed = true; }
        ImGui::PopStyleColor(5);
        ImGui::PopID();
        x += w;
    }
    ImGui::PopID();
    return changed;
}

int TickFormat(double v, char *buf, int size, void *) {
    std::string s;
    double a = std::fabs(v);
    if (a >= 1000) s = fint(v);
    else if (a >= 1 || a == 0) { char b[32]; std::snprintf(b, sizeof b, "%g", v); s = b; }
    else { char b[32]; std::snprintf(b, sizeof b, "%.3g", v); s = b; }
    return std::snprintf(buf, (size_t)size, "%s", s.c_str());
}

/* ======================================================================== PNG */
static unsigned crcTable[256];
static void crcInit() {
    for (unsigned n = 0; n < 256; n++) {
        unsigned c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
static unsigned crc(const unsigned char *b, size_t n, unsigned c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ b[i]) & 255] ^ (c >> 8);
    return c;
}
static void be32(std::vector<unsigned char> &o, unsigned v) {
    o.push_back((unsigned char)(v >> 24)); o.push_back((unsigned char)(v >> 16));
    o.push_back((unsigned char)(v >> 8)); o.push_back((unsigned char)v);
}
static void chunk(std::vector<unsigned char> &out, const char *type, const std::vector<unsigned char> &data) {
    be32(out, (unsigned)data.size());
    std::vector<unsigned char> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

bool writePng(const std::string &path, int w, int h, const std::vector<unsigned char> &rgba) {
    crcInit();
    std::vector<unsigned char> raw;
    raw.reserve((size_t)(w * 3 + 1) * (size_t)h);
    for (int y = h - 1; y >= 0; y--) {          // OpenGL rows are bottom-up
        raw.push_back(0);
        const unsigned char *row = &rgba[(size_t)y * (size_t)w * 4];
        for (int x = 0; x < w; x++) { raw.push_back(row[x * 4]); raw.push_back(row[x * 4 + 1]); raw.push_back(row[x * 4 + 2]); }
    }
    std::vector<unsigned char> z = { 0x78, 0x01 };   // zlib header, stored blocks
    size_t pos = 0;
    while (pos < raw.size() || raw.empty()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((unsigned char)(n & 255)); z.push_back((unsigned char)(n >> 8));
        z.push_back((unsigned char)(~n & 255)); z.push_back((unsigned char)((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + (long)pos, raw.begin() + (long)(pos + n));
        pos += n;
        if (last) break;
    }
    unsigned a = 1, b = 0;
    for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    std::vector<unsigned char> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<unsigned char> ihdr;
    be32(ihdr, (unsigned)w); be32(ihdr, (unsigned)h);
    ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 });      // 8-bit RGB
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

} // namespace ev33
