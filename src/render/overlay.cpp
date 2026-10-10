#include "render/overlay.h"

#include "app/log.h"
#include "resource.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>

namespace render {
namespace {

// ---- words ---------------------------------------------------------------------------------

const wchar_t* const kTitle = L"The Sun: The Life of a Photon";

// The photon's stages, and how long the energy spends in each. Core and radiative zone together
// take ~170,000 years (Mitalas & Sills 1992); the split between them is this project's estimate
// from a standard solar model's density and opacity profile (The Sun References,
// physics/photon-time-by-layer.md).
const wchar_t* const kStageNames[sim::kStageCount] = {
    L"Core", L"Radiative zone", L"Tachocline", L"Convective zone", L"Photosphere", L"Into space",
};
const wchar_t* const kStageTimes[sim::kStageCount] = {
    L"~80,000 years", L"~87,000 years", L"~3,000 years", L"~10 days", L"a few seconds", L"8 min 20 s to Earth",
};

// Two lines each, shown left-justified along the bottom of the screen.
const wchar_t* const kCaptions[sim::kCaptionCount][2] = {
    {L"The Sun, cut open: its core, radiative zone and convective zone.",
     L"Follow the energy of one photon on its way out."},
    {L"In the core, at 15 million kelvin, hydrogen fuses into helium.",
     L"The energy is born as a gamma-ray photon."},
    {L"The gas is so dense that a photon goes about a millimetre before it is absorbed.",
     L"Re-emitted in a random direction each time, it wanders: a random walk."},
    {L"Absorbed and re-emitted countless times, the energy drifts outward,",
     L"softening from gamma rays to X-rays to ultraviolet."},
    {L"No single photon makes the trip: the energy passes from one to the next.",
     L"Estimates for this stage run from 10,000 to a million years."},
    {L"At the tachocline the evenly turning interior meets the churning outer layers.",
     L"The shear here is thought to drive the Sun's magnetic field."},
    {L"Here the gas is too opaque for light to carry the energy.",
     L"Hot plasma rises, cools at the top and sinks again, like a pot on the boil."},
    {L"At the photosphere the gas finally turns transparent.",
     L"The energy, now visible light, escapes at the speed of light."},
    {L"About 170,000 years after it was made in the core,",
     L"the light reaches Earth 8 minutes and 20 seconds after leaving the surface."},
};

// ---- look ----------------------------------------------------------------------------------

constexpr float kInk[4]    = {0.92f, 0.94f, 1.00f, 0.92f};  // labels and captions: a cool white
constexpr float kAnts[4]   = {0.62f, 0.92f, 1.00f, 0.95f};  // the photon: cyan, against the orange Sun
constexpr float kLeader[4] = {0.92f, 0.94f, 1.00f, 0.65f};
constexpr float kShadow[4] = {0.02f, 0.03f, 0.06f, 0.55f};
constexpr float kTextShadow[4] = {0.0f, 0.0f, 0.0f, 0.7f};
constexpr float kTextHalo[4]   = {0.0f, 0.0f, 0.0f, 0.45f};

// Font sizes in the atlas, pixels of em height: big enough to stay sharp on a 1440p screen.
constexpr int kTitleEm = 112, kLabelEm = 52, kTimeEm = 44, kCaptionEm = 104;

// The crawling ants: dash period and how fast they crawl, in screen pixels at 1080 lines.
constexpr float kDashPeriod = 15.0f, kCrawl = 26.0f;

float Smooth(float a, float b, float x) { return sim::SmoothStep(a, b, x); }

// ---- geometry ------------------------------------------------------------------------------

struct Px {
    float x, y;
};

struct Writer {
    float                       width, height;
    std::vector<OverlayVertex>& out;

    OverlayVertex V(Px p) const {
        OverlayVertex v{};
        v.pos[0] = p.x / width * 2.0f - 1.0f;  // Vulkan NDC: y down, like pixels
        v.pos[1] = p.y / height * 2.0f - 1.0f;
        return v;
    }

    // An anti-aliased line, `w` pixels wide. Ends are extended by half the width, so lines that
    // meet at a corner close it. With `dashed`, the dash pattern runs from `alongA` at `a` to
    // `alongB` at `b`.
    void Line(Px a, Px b, float w, const float color[4], float alpha, bool dashed = false, float alongA = 0.0f,
              float alongB = 0.0f) {
        float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-3f) return;
        dx /= len;
        dy /= len;
        const float half = 0.5f * w, reach = half + 1.0f;  // one pixel of feather each side
        const float ext  = dashed ? 0.0f : half;
        const Px    a2{a.x - dx * ext, a.y - dy * ext}, b2{b.x + dx * ext, b.y + dy * ext};
        const float nx = -dy * reach, ny = dx * reach;
        const Px    corners[4] = {{a2.x + nx, a2.y + ny}, {b2.x + nx, b2.y + ny}, {b2.x - nx, b2.y - ny}, {a2.x - nx, a2.y - ny}};
        const float across[4]  = {reach, reach, -reach, -reach};
        const float along[4]   = {alongA, alongB, alongB, alongA};
        const int   order[6]   = {0, 1, 2, 0, 2, 3};
        for (int i : order) {
            OverlayVertex v = V(corners[i]);
            std::memcpy(v.color, color, sizeof(float) * 3);
            v.color[3]  = alpha * color[3];
            v.mode      = dashed ? 2.0f : 1.0f;
            v.halfWidth = half;
            v.across    = across[i];
            v.along     = along[i];
            out.push_back(v);
        }
    }

    // A string from the atlas with the top-left of its cell at `at`, scaled by `scale`, over a soft
    // drop shadow. Every string gets one: few need it against black space, but the captions cross
    // the bright Sun, and the same treatment everywhere keeps the type consistent.
    void Text(const TextRun& run, Px at, float scale, const float color[4], float alpha) {
        // A dark halo round the letters, then a drop to the lower right.
        const float h = std::max(1.0f, 0.035f * run.height * scale), d = 2.0f * h;
        const float halo[4][2] = {{-h, -h}, {h, -h}, {-h, h}, {h, h}};
        for (const auto& o : halo) Glyphs(run, {at.x + o[0], at.y + o[1]}, scale, kTextHalo, alpha);
        Glyphs(run, {at.x + d, at.y + d}, scale, kTextShadow, alpha);
        Glyphs(run, at, scale, color, alpha);
    }

    // A line under text, with the same shadow.
    void Underline(Px a, Px b, float w, const float color[4], float alpha) {
        const float off = std::max(1.5f, 1.2f * w);
        Line({a.x + off, a.y + off}, {b.x + off, b.y + off}, w, kTextShadow, alpha);
        Line(a, b, w, color, alpha);
    }

    void Glyphs(const TextRun& run, Px at, float scale, const float color[4], float alpha) {
        const float w = run.width * scale, h = run.height * scale;
        const Px    corners[4] = {{at.x, at.y}, {at.x + w, at.y}, {at.x + w, at.y + h}, {at.x, at.y + h}};
        const float uvs[4][2]  = {{run.u0, run.v0}, {run.u1, run.v0}, {run.u1, run.v1}, {run.u0, run.v1}};
        const int   order[6]   = {0, 1, 2, 0, 2, 3};
        for (int i : order) {
            OverlayVertex v = V(corners[i]);
            v.uv[0] = uvs[i][0];
            v.uv[1] = uvs[i][1];
            std::memcpy(v.color, color, sizeof(float) * 3);
            v.color[3] = alpha * color[3];
            out.push_back(v);
        }
    }
};

bool ToScreen(const OverlayView& view, sim::Vec3 p, Px& out, float* w = nullptr) {
    float clip[4];
    sim::Project(view.viewProj, p, 1.0f, clip);
    if (clip[3] <= 1e-4f) return false;
    out = {(clip[0] / clip[3] * 0.5f + 0.5f) * view.width, (clip[1] / clip[3] * 0.5f + 0.5f) * view.height};
    if (w) *w = clip[3];
    return true;
}

}  // namespace

// ---- the text atlas ------------------------------------------------------------------------

TextAtlas BuildTextAtlas() {
    TextAtlas atlas;

    // The font comes from the .scr itself, private to this process: never installed.
    HMODULE     module = GetModuleHandleW(nullptr);
    HRSRC       found  = FindResourceW(module, MAKEINTRESOURCEW(IDR_OVERLAY_FONT), MAKEINTRESOURCEW(10));
    HGLOBAL     loaded = found ? LoadResource(module, found) : nullptr;
    void*       data   = loaded ? LockResource(loaded) : nullptr;
    const DWORD size   = found ? SizeofResource(module, found) : 0;
    DWORD       fonts  = 0;
    HANDLE      added  = data ? AddFontMemResourceEx(data, size, nullptr, &fonts) : nullptr;
    if (!added) {
        app::Log("overlay: Michroma could not be loaded");
        return atlas;
    }

    HDC  dc       = CreateCompatibleDC(nullptr);
    auto makeFont = [](int em) {
        return CreateFontW(-em, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Michroma");
    };
    HFONT title = makeFont(kTitleEm), label = makeFont(kLabelEm), time = makeFont(kTimeEm),
          caption = makeFont(kCaptionEm);

    struct Item {
        const wchar_t* text;
        HFONT          font;
        TextRun*       run;
        SIZE           size{};
        int            ascent = 0;
        int            x = 0, y = 0;
    };
    std::vector<Item> items;
    items.push_back({kTitle, title, &atlas.title});
    const sim::Planet* planets = sim::Planets();
    for (int i = 0; i < sim::kPlanetCount; ++i) items.push_back({planets[i].name, label, &atlas.planets[i]});
    for (int i = 0; i < sim::kStageCount; ++i) {
        items.push_back({kStageNames[i], label, &atlas.stageNames[i]});
        items.push_back({kStageTimes[i], time, &atlas.stageTimes[i]});
    }
    for (int i = 0; i < sim::kCaptionCount; ++i) {
        for (int l = 0; l < 2; ++l) items.push_back({kCaptions[i][l], caption, &atlas.captions[i][l]});
    }

    // Measure, then pack into rows (shelves) no wider than kMaxWidth, with padding for the mips.
    constexpr int kMaxWidth = 8192, pad = 8;  // the longest caption line is ~4,600 px at its em
    wchar_t       face[64]  = {};
    for (Item& it : items) {
        HGDIOBJ old = SelectObject(dc, it.font);
        GetTextExtentPoint32W(dc, it.text, static_cast<int>(wcslen(it.text)), &it.size);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        it.ascent = tm.tmAscent;
        GetTextFaceW(dc, 64, face);
        SelectObject(dc, old);
    }
    if (std::wcscmp(face, L"Michroma") != 0) app::Log("overlay: GDI chose %ls, not Michroma", face);
    int x = pad, y = pad, rowHeight = 0, w = 0;
    for (Item& it : items) {
        const int cw = std::min<int>(it.size.cx, kMaxWidth - 2 * pad);
        if (x + cw + pad > kMaxWidth) {
            x = pad;
            y += rowHeight + 2 * pad;
            rowHeight = 0;
        }
        it.x      = x;
        it.y      = y;
        x += cw + 2 * pad;
        rowHeight = std::max<int>(rowHeight, it.size.cy);
        w         = std::max(w, x);
    }
    const int h = y + rowHeight + pad;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;  // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void*   bits = nullptr;
    HBITMAP bmp  = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ oldBmp = SelectObject(dc, bmp);
        std::memset(bits, 0, std::size_t(w) * h * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        for (Item& it : items) {
            HGDIOBJ old = SelectObject(dc, it.font);
            TextOutW(dc, it.x, it.y, it.text, static_cast<int>(wcslen(it.text)));
            SelectObject(dc, old);
            it.run->u0       = float(it.x) / w;
            it.run->v0       = float(it.y) / h;
            it.run->u1       = float(it.x + it.size.cx) / w;
            it.run->v1       = float(it.y + it.size.cy) / h;
            it.run->width    = float(it.size.cx);
            it.run->height   = float(it.size.cy);
            it.run->baseline = float(it.ascent);
        }
        GdiFlush();
        // White on black: the green channel is the coverage. Put it in every channel.
        const auto* src = static_cast<const std::uint8_t*>(bits);
        atlas.rgba.resize(std::size_t(w) * h * 4);
        for (std::size_t i = 0; i < std::size_t(w) * h; ++i) std::memset(&atlas.rgba[i * 4], src[i * 4 + 1], 4);
        atlas.width  = static_cast<std::uint32_t>(w);
        atlas.height = static_cast<std::uint32_t>(h);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
    }
    for (HFONT f : {title, label, time, caption}) DeleteObject(f);
    DeleteDC(dc);
    RemoveFontMemResourceEx(added);
    app::Log("overlay: text atlas %ux%u, %u strings", atlas.width, atlas.height, static_cast<unsigned>(items.size()));
    return atlas;
}

// ---- the overlay ---------------------------------------------------------------------------

Overlay::Overlay(const TextAtlas& atlas) : atlas_(atlas) {
    atlas_.rgba.clear();
    atlas_.rgba.shrink_to_fit();
    // SUN_PHOTON_OVERLAYS: whatever overlay the moment has is drawn at full strength, for captures.
    forced_ = GetEnvironmentVariableW(L"SUN_PHOTON_OVERLAYS", nullptr, 0) > 0;
}

void Overlay::Emit(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const {
    if (view.height < 300.0f || atlas_.title.height <= 0.0f) return;  // the preview pane: too small to read
    Planets(m, view, out);
    PhotonPath(m, view, out);
    Captions(m, view, out);
    Title(m, view, out);
}

void Overlay::Planets(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const {
    Writer             w{view.width, view.height, out};
    const sim::Planet* planets = sim::Planets();
    // Named on the flights only: at the Sun the labels belong to the photon.
    if (m.sinceArrival >= 0.0f && m.sinceArrival < sim::Timing().atSun) return;
    const float        cell    = 0.030f * view.height;
    for (int i = 0; i < sim::kPlanetCount; ++i) {
        const sim::Planet& p = planets[i];
        Px                 c{};
        float              depth = 0.0f;
        if (!ToScreen(view, p.position, c, &depth)) continue;
        const float r = p.radius * view.focal / depth;  // its radius on screen, pixels
        // Named once it is a recognisable disk, and only while it is well inside the frame.
        const float margin = 0.04f * view.height;
        const float inside = Smooth(-margin, margin, std::min({c.x, c.y, view.width - c.x, view.height - c.y}) - r * 0.2f);
        float       alpha  = Smooth(2.5f, 9.0f, r) * inside;
        if (forced_ && r > 1.5f && inside > 0.0f) alpha = 1.0f;
        if (alpha < 0.003f) continue;

        const TextRun& run   = atlas_.planets[i];
        const float    scale = cell / run.height;
        const float    textW = run.width * scale;
        // Under the disk, centred on it. Kept on screen, so a planet larger than the frame keeps
        // its name along the bottom edge.
        const float x = std::clamp(c.x - 0.5f * textW, margin, view.width - margin - textW);
        const float y = std::clamp(c.y + r + 0.5f * cell, margin, view.height - margin - cell);
        w.Text(run, {x, y}, scale, kInk, alpha);
    }
}

void Overlay::PhotonPath(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const {
    if (m.photonAlpha <= 0.003f) return;
    Writer                 w{view.width, view.height, out};
    const sim::PhotonPath& path  = sim::Photon();
    const float            k     = view.height / 1080.0f;
    const float            alpha = m.photonAlpha;

    // The revealed part of the path, on screen, with the stage of each segment.
    const int   last = std::min(static_cast<int>(m.photonHead), static_cast<int>(path.points.size()) - 1);
    const float frac = m.photonHead - static_cast<float>(last);
    std::vector<Px> pts;
    pts.reserve(last + 2);
    for (int i = 0; i <= last; ++i) {
        Px p{};
        if (!ToScreen(view, sim::PathToWorld(m, path.points[i]), p)) return;
        pts.push_back(p);
    }
    if (frac > 0.0f && last + 1 < static_cast<int>(path.points.size())) {
        const sim::PathPoint a = path.points[last], b = path.points[last + 1];
        Px p{};
        if (ToScreen(view, sim::PathToWorld(m, {a.r + (b.r - a.r) * frac, a.a + (b.a - a.a) * frac}), p)) pts.push_back(p);
    }
    if (pts.size() < 2) return;

    // Back and forth (the random walk, the rising and sinking of convection): two parallel lines,
    // their ants crawling in opposite directions. One way (out of the photosphere): one line.
    const float lineW  = std::max(2.4f * k, 1.0f);
    const float offset = 3.0f * k;
    const float crawl  = kCrawl * k * m.t;
    const float period = kDashPeriod * k;
    auto stageOf = [&](int segment) {
        int s = 0;
        while (s + 1 < sim::kStageCount && segment >= path.stageBegin[s + 1]) ++s;
        return s;
    };
    // A dark band under the whole trail first, so the ants stand out against the bright faces.
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const bool oneWay = stageOf(static_cast<int>(i)) >= 4;
        w.Line(pts[i], pts[i + 1], oneWay ? lineW * 2.6f : 2.0f * offset + lineW * 2.2f, kShadow, alpha);
    }
    float along = 0.0f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const Px    a = pts[i], b = pts[i + 1];
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.01f) continue;
        const float nx = -dy / len, ny = dx / len;
        const bool  oneWay = stageOf(static_cast<int>(i)) >= 4;
        // A segment's age dims it a little, so the head stands out from the trail behind it.
        const float age  = static_cast<float>(pts.size() - 1 - i);
        const float fade = alpha * (0.55f + 0.45f * std::exp(-age / 40.0f));
        if (oneWay) {
            w.Line(a, b, lineW * 1.15f, kAnts, fade, true, (along - crawl) / period, (along + len - crawl) / period);
        } else {
            const Px a1{a.x + nx * offset, a.y + ny * offset}, b1{b.x + nx * offset, b.y + ny * offset};
            const Px a2{a.x - nx * offset, a.y - ny * offset}, b2{b.x - nx * offset, b.y - ny * offset};
            w.Line(a1, b1, lineW, kAnts, fade, true, (along - crawl) / period, (along + len - crawl) / period);
            w.Line(a2, b2, lineW, kAnts, fade, true, (along + crawl) / period, (along + len + crawl) / period);
        }
        along += len;
    }

    // The head: a short bright bar across the path's end.
    const Px head = pts.back();
    w.Line({head.x - 3.5f * k, head.y}, {head.x + 3.5f * k, head.y}, 7.0f * k, kAnts, alpha);

    // The stage label sits to the right of the Sun, level with the photon, with a leader to it.
    if (m.stage < 0) return;
    const float a = forced_ ? 1.0f : m.stageAlpha * alpha;
    if (a <= 0.003f) return;
    Px centre{}, limb{};
    float depth = 0.0f;
    if (!ToScreen(view, sim::Vec3{}, centre, &depth)) return;
    const float sunR = view.focal / depth;  // pixels
    (void)limb;

    const TextRun& name  = atlas_.stageNames[m.stage];
    const TextRun& time  = atlas_.stageTimes[m.stage];
    const float    cell  = 0.030f * view.height;
    const float    scale = cell / name.height;
    const float    tcell = 0.024f * view.height;
    const float    tscale = tcell / time.height;
    const float    textW = std::max(name.width * scale, time.width * tscale);
    const float    margin = 0.04f * view.height;
    float          x0    = std::min(centre.x + sunR * 1.12f, view.width - margin - textW);
    x0                   = std::max(x0, head.x + 0.06f * view.height);
    const float ulY      = std::clamp(head.y - 0.11f * view.height, margin + cell * 1.5f, view.height - 0.22f * view.height);
    const float top      = ulY - 0.18f * cell - name.baseline * scale;
    w.Text(name, {x0, top}, scale, kInk, a);
    w.Underline({x0, ulY}, {x0 + textW, ulY}, std::max(1.4f * k, 1.0f), kInk, a);
    w.Text(time, {x0, ulY + 0.25f * tcell}, tscale, kInk, a);
    // The leader, from the underline's inner end to just short of the photon.
    const float lx = head.x - x0, ly = head.y - ulY;
    const float ll = std::sqrt(lx * lx + ly * ly);
    if (ll > 12.0f * k) {
        const Px end{head.x - lx / ll * 9.0f * k, head.y - ly / ll * 9.0f * k};
        w.Line({x0, ulY}, end, std::max(1.2f * k, 1.0f), kLeader, a);
    }
}

void Overlay::Captions(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const {
    if (m.caption < 0) return;
    const float a = forced_ ? 1.0f : m.captionAlpha;
    if (a <= 0.003f) return;
    Writer      w{view.width, view.height, out};
    const float margin = 0.045f * view.height;
    const float cell   = 0.052f * view.height;
    const TextRun* lines = atlas_.captions[m.caption];
    // One size for both lines, shrunk if needed so the longer fits the screen's width.
    const float widest = std::max(lines[0].width, lines[1].width);
    const float scale  = std::min(cell / lines[0].height, (view.width - 2.0f * margin) / widest);
    const float lineH  = lines[0].height * scale * 1.25f;
    const float bottom = view.height - margin;
    for (int l = 0; l < 2; ++l) {
        const float top = bottom - (2 - l) * lineH;
        w.Text(lines[l], {margin, top}, scale, kInk, a);
    }
}

void Overlay::Title(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const {
    float alpha = m.titleAlpha;
    if (forced_ && m.sinceArrival >= 0.0f && m.sinceArrival < sim::Timing().wedgeOpen.start) alpha = 1.0f;
    if (alpha <= 0.003f) return;
    Writer      w{view.width, view.height, out};
    const float k      = view.height / 1080.0f;
    const float cell   = 0.056f * view.height;
    const float scale  = std::min(cell / atlas_.title.height, 0.62f * view.width / atlas_.title.width);
    const float margin = 0.055f * view.height;
    const float right  = view.width - margin * 1.2f;
    const float textW  = atlas_.title.width * scale;
    const float ulY    = view.height - margin;
    const float top    = ulY - 0.16f * cell - atlas_.title.baseline * scale;
    w.Text(atlas_.title, {right - textW, top}, scale, kInk, alpha);
    w.Underline({right - textW, ulY}, {right, ulY}, std::max(2.0f * k, 1.0f), kInk, alpha);
}

}  // namespace render
