// ui.cpp - theme, fonts, small widgets and PNG export for the EV33 Charge Atlas desktop app.
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ev33 {

const char *const VIEW_NAMES[VIEW_COUNT] = { "Dashboard", "Trip Planner", "Station Placement", "Road Network",
                                             "Algorithms", "Benchmarks", "Data Management", "Settings" };
const char *const VIEW_KEYS[VIEW_COUNT] = { "dashboard", "trip", "placement", "network", "algorithms", "benchmarks", "data", "settings" };

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
    if (!dark) {   // warm light
        p.bg = hex(0xF3F1EC); p.panel = hex(0xFBFAF7); p.panel2 = hex(0xEBE7DF); p.line = hex(0xD8D2C6);
        p.ink = hex(0x1E2421); p.ink2 = hex(0x4E5752); p.ink3 = hex(0x858B86); p.contour = hex(0xC9C2B2);
        p.primary = hex(0x2E6A4F); p.primarySoft = hex(0xDAE8DF);
        p.secondary = hex(0x4B6D8E); p.secondarySoft = hex(0xDDE6EF);
        p.warn = hex(0xA4670F); p.warnSoft = hex(0xF3E5C9);
        p.error = hex(0xB13A31); p.errorSoft = hex(0xF4DCD8);
    } else {       // deep charcoal
        p.bg = hex(0x1A1D1C); p.panel = hex(0x212524); p.panel2 = hex(0x2A2F2D); p.line = hex(0x3A403D);
        p.ink = hex(0xE6E8E3); p.ink2 = hex(0xB2B8B3); p.ink3 = hex(0x7E8681); p.contour = hex(0x3E4842);
        p.primary = hex(0x6BB18B); p.primarySoft = hex(0x20382D);
        p.secondary = hex(0x89A9C8); p.secondarySoft = hex(0x22303D);
        p.warn = hex(0xE0A84A); p.warnSoft = hex(0x3A2F1B);
        p.error = hex(0xE4796E); p.errorSoft = hex(0x3D2420);
    }
    return p;
}

void applyTheme(const Palette &p, bool dark, float scale) {
    ImGuiStyle st;
    if (dark) ImGui::StyleColorsDark(&st); else ImGui::StyleColorsLight(&st);
    st.WindowPadding = ImVec2(14, 12);
    st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 7);
    st.ItemInnerSpacing = ImVec2(6, 5);
    st.CellPadding = ImVec2(8, 5);
    st.IndentSpacing = 16;
    st.ScrollbarSize = 11;
    st.GrabMinSize = 10;
    st.WindowRounding = 0;
    st.ChildRounding = 4;
    st.FrameRounding = 3;
    st.PopupRounding = 4;
    st.GrabRounding = 3;
    st.TabRounding = 3;
    st.ScrollbarRounding = 3;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.FrameBorderSize = 1;
    st.PopupBorderSize = 1;
    st.SeparatorTextBorderSize = 1;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = p.ink;
    c[ImGuiCol_TextDisabled] = p.ink3;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_Border] = p.line;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = p.panel;
    c[ImGuiCol_FrameBgHovered] = p.panel2;
    c[ImGuiCol_FrameBgActive] = p.panel2;
    c[ImGuiCol_TitleBg] = p.panel2;
    c[ImGuiCol_TitleBgActive] = p.panel2;
    c[ImGuiCol_MenuBarBg] = p.panel2;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.line;
    c[ImGuiCol_ScrollbarGrabHovered] = p.ink3;
    c[ImGuiCol_ScrollbarGrabActive] = p.ink2;
    c[ImGuiCol_CheckMark] = p.primary;
    c[ImGuiCol_SliderGrab] = p.primary;
    c[ImGuiCol_SliderGrabActive] = p.primary;
    c[ImGuiCol_Button] = p.panel2;
    c[ImGuiCol_ButtonHovered] = mix(p.panel2, p.line, 0.6f);
    c[ImGuiCol_ButtonActive] = p.primarySoft;
    c[ImGuiCol_Header] = p.primarySoft;
    c[ImGuiCol_HeaderHovered] = p.panel2;
    c[ImGuiCol_HeaderActive] = p.primarySoft;
    c[ImGuiCol_Separator] = p.line;
    c[ImGuiCol_SeparatorHovered] = p.ink3;
    c[ImGuiCol_SeparatorActive] = p.primary;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = p.bg;
    c[ImGuiCol_TabHovered] = p.panel2;
    c[ImGuiCol_TabSelected] = p.panel;
    c[ImGuiCol_TabSelectedOverline] = p.primary;
    c[ImGuiCol_TabDimmed] = p.bg;
    c[ImGuiCol_TabDimmedSelected] = p.panel;
    c[ImGuiCol_TableHeaderBg] = p.panel2;
    c[ImGuiCol_TableBorderStrong] = p.line;
    c[ImGuiCol_TableBorderLight] = mix(p.line, p.panel, 0.4f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(p.panel2.x, p.panel2.y, p.panel2.z, 0.45f);
    c[ImGuiCol_TextSelectedBg] = p.primarySoft;
    c[ImGuiCol_NavHighlight] = p.primary;
    c[ImGuiCol_PlotHistogram] = p.primary;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.3f);
    st.ScaleAllSizes(scale);
    ImGui::GetStyle() = st;
}

/* ======================================================================== fonts */
bool loadFonts(Fonts &f, const std::string &dir, float scale) {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0370, 0x03FF, 0x2000, 0x206F, 0x2070, 0x209F, 0x20A0, 0x20CF,
                                      0x2190, 0x21FF, 0x2200, 0x22FF, 0x25A0, 0x25FF, 0 };
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
    f = Fonts();
    f.body = load("IBMPlexSans-Regular.ttf", 15);
    if (!f.body) {   // fonts missing: Dear ImGui's built-in font everywhere
        ImFontConfig cfg;
        cfg.SizePixels = 13 * scale;
        f.body = io.Fonts->AddFontDefault(&cfg);
        f.semi = f.bold = f.small = f.smallSemi = f.h1 = f.h2 = f.mono = f.monoBig = f.body;
        return false;
    }
    f.fromFiles = true;
    f.semi = load("IBMPlexSans-SemiBold.ttf", 15);
    f.bold = load("IBMPlexSans-Bold.ttf", 15);
    f.small = load("IBMPlexSans-Regular.ttf", 13);
    f.smallSemi = load("IBMPlexSans-SemiBold.ttf", 13);
    f.h2 = load("IBMPlexSans-SemiBold.ttf", 18);
    f.h1 = load("IBMPlexSans-SemiBold.ttf", 23);
    f.mono = load("IBMPlexMono-Regular.ttf", 13.5f);
    f.monoBig = load("IBMPlexMono-Regular.ttf", 24);
    if (!f.semi) f.semi = f.body;
    if (!f.bold) f.bold = f.semi;
    if (!f.smallSemi) f.smallSemi = f.small;
    if (!f.mono) f.mono = f.body;
    if (!f.monoBig) f.monoBig = f.h1;
    return true;
}

/* ======================================================================== widgets */
void RichText(const std::string &s, const Fonts &f, ImU32 color) {
    float wrap = ImGui::GetContentRegionAvail().x - 2;
    ImGui::PushTextWrapPos(-1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
    ImGui::BeginGroup();
    bool bold = false, first = true;
    float x = 0;
    ImFont *base = ImGui::GetFont();
    ImFont *boldF = base == f.small ? f.smallSemi : f.semi;
    float size = ImGui::GetFontSize();
    float space = base->CalcTextSizeA(size, 1e9f, 0, " ").x;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\n') { first = true; x = 0; i++; continue; }
        if (s[i] == ' ') { i++; continue; }
        size_t j = i;
        std::string word;
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
        for (auto &pt : parts) w += std::ceil((pt.second ? boldF : base)->CalcTextSizeA(size, 1e9f, 0, pt.first.c_str()).x);
        if (!first) {
            if (x + space + w > wrap) x = 0;
            else { ImGui::SameLine(0, space); x += space; }
        }
        bool firstPart = true;
        for (auto &pt : parts) {
            if (!firstPart) ImGui::SameLine(0, 0);
            ImGui::PushFont(pt.second ? boldF : base);
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

void ViewHeader(App &app, const char *title, const char *purpose) {
    ImGui::PushFont(app.fonts.h1);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (purpose && *purpose) {
        ImGui::PushStyleColor(ImGuiCol_Text, app.pal.ink2);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(ImGui::GetContentRegionAvail().x, 820.f * app.scale));
        ImGui::TextWrapped("%s", purpose);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 4 * app.scale));
}

void SectionTitle(App &app, const char *title, const char *note) {
    ImGui::PushFont(app.fonts.h2);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (note && *note) {
        ImGui::PushFont(app.fonts.small);
        ImGui::PushStyleColor(ImGuiCol_Text, app.pal.ink3);
        ImGui::TextWrapped("%s", note);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
}

void Tag(const char *text, const ImVec4 &bg, const ImVec4 &fg) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    float s = ImGui::GetFontSize() / 13.f;
    ImVec2 pad(6 * s, 2 * s);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ts.x + pad.x * 2, ts.y + pad.y * 2);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), col(bg), 2 * s);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), col(fg), text);
    ImGui::Dummy(sz);
}

void SyntheticTag(App &app, const Scene &sc) {
    ImGui::PushFont(app.fonts.smallSemi);
    if (sc.cfg.source == "synthetic") Tag("SYNTHETIC DATASET", app.pal.warnSoft, app.pal.warn);
    else if (sc.cfg.source == "real") Tag("REAL-WORLD DATA (as declared in scenario.cfg)", app.pal.secondarySoft, app.pal.secondary);
    else Tag("MIXED REAL AND SYNTHETIC DATA", app.pal.warnSoft, app.pal.warn);
    ImGui::PopFont();
    if (ImGui::IsItemHovered() && !sc.cfg.source_note.empty()) ImGui::SetTooltip("Source: %s", sc.cfg.source_note.c_str());
}

bool Segmented(const char *id, const char *const *labels, int n, int *sel, const Palette &p, float width) {
    ImGui::PushID(id);
    float avail = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    float w = avail / n;
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    for (int i = 0; i < n; i++) {
        bool on = *sel == i;
        ImGui::PushID(i);
        if (i) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, on ? p.primary : p.panel);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? p.primary : p.panel2);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? p.primary : p.panel2);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? p.panel : p.ink);
        if (ImGui::Button(labels[i], ImVec2(w, 0)) && !on) { *sel = i; changed = true; }
        ImGui::PopStyleColor(4);
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

float BeginPanel(App &app, const char *id, ImVec2 size, bool scroll) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, app.pal.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * app.scale, 10 * app.scale));
    ImGuiChildFlags cf = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (size.y == 0) cf |= ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild(id, size, cf, scroll ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    return ImGui::GetContentRegionAvail().x;
}
void EndPanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void Metric(App &app, const char *id, const char *label, const std::string &value, const std::string &note, float width, float height,
            const ImVec4 *accent) {
    const Palette &P = app.pal;
    BeginPanel(app, id, ImVec2(width, height));
    if (accent) {
        ImVec2 a = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + 3 * app.scale, a.y + ImGui::GetWindowHeight()), col(*accent));
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink2, "%s", label);
    ImGui::PopFont();
    ImGui::PushFont(app.fonts.monoBig);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    bool cut = false;
    if (!note.empty()) {
        // one line; a note too long for the card is shortened with "..." and shown whole on hover
        ImGui::PushFont(app.fonts.small);
        ImGui::PushStyleColor(ImGuiCol_Text, P.ink3);
        float avail = ImGui::GetContentRegionAvail().x;
        std::string shown = note;
        if (ImGui::CalcTextSize(shown.c_str()).x > avail) {
            cut = true;
            float dots = ImGui::CalcTextSize("...").x;
            size_t n = shown.size();
            while (n > 0 && ImGui::CalcTextSize(note.c_str(), note.c_str() + n).x + dots > avail) {
                n--;
                while (n > 0 && (static_cast<unsigned char>(note[n]) & 0xC0) == 0x80) n--;   // keep UTF-8 sequences whole
            }
            while (n > 0 && note[n - 1] == ' ') n--;
            shown = note.substr(0, n) + "...";
        }
        ImGui::TextUnformatted(shown.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    bool hover = cut && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    EndPanel();
    if (hover) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
        ImGui::TextUnformatted(note.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void HelpMarker(const char *text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
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
