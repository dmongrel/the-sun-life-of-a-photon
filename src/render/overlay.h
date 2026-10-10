// The overlays: the title credit, planet names, the photon's crawling-ants path with its layer
// labels and leader line, and the explanatory captions.
//
// Text is set once, at start-up, by GDI in the embedded Michroma font, into a texture atlas. Every
// frame the overlay is rebuilt as a list of quads: text quads sample the atlas, line quads (solid
// or dashed) are anti-aliased in the fragment shader. They are drawn over the finished image.
// Everything here is a function of the loop time, so a capture at a given moment always matches.
#pragma once

#include "sim/system.h"

#include <cstdint>
#include <vector>

namespace render {

// Must match `Vertex` in shaders/overlay.vert (std430, three vec4s).
struct OverlayVertex {
    float pos[2];     // NDC
    float uv[2];      // text: texture coordinates; soft line: pixels along it, its length
    float color[4];   // straight alpha
    float mode;       // 0 text, 1 line, 2 dashed line, 3 soft line
    float halfWidth;  // line: half its width, pixels
    float across;     // line: signed distance from its centre line, pixels
    float along;      // dashed line: distance along it, pixels, for the dash pattern
};

// One string as set in the atlas.
struct TextRun {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float width = 0, height = 0;  // the cell, atlas pixels
    float baseline = 0;           // from the top of the cell
};

struct TextAtlas {
    std::uint32_t             width = 0, height = 0;
    std::vector<std::uint8_t> rgba;  // coverage in every channel
    TextRun                   title;
    TextRun                   planets[sim::kPlanetCount];
    TextRun                   stageNames[sim::kStageCount];
    TextRun                   stageTimes[sim::kStageCount];
    TextRun                   captions[sim::kCaptionCount][2];
};

// Empty `rgba` if the font or GDI fails; the overlays are then simply not drawn.
TextAtlas BuildTextAtlas();

// What one window needs to place the overlays this frame.
struct OverlayView {
    sim::Mat4 viewProj;
    float     width = 0, height = 0;  // pixels
    float     focal = 0;              // pixels per unit at unit distance
};

class Overlay {
public:
    explicit Overlay(const TextAtlas& atlas);

    // Appends this window's quads for moment `m` to `out`.
    void Emit(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const;

private:
    void Planets(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const;
    void PhotonPath(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const;
    void Captions(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const;
    void Title(const sim::Moment& m, const OverlayView& view, std::vector<OverlayVertex>& out) const;

    TextAtlas atlas_;  // runs only; the pixels live on the GPU
    bool      forced_ = false;
};

}  // namespace render
