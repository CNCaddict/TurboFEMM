// FEMM Qt 6 GUI — Shared results overlay renderer implementation
// All overlay layers (density, contours, mesh) are rasterized directly
// to a QImage pixel buffer, then blitted with a single drawImage() call.
// This avoids per-primitive QPainter overhead entirely.
//
// Density uses Gouraud (per-pixel interpolated) shading with a 256-color
// smooth palette for band-free gradients.
//
// AA modes:
//   None — Bresenham lines, hard-edge triangles (fastest)
//   Low  — Wu's AA lines for contours/mesh, hard-edge triangles
//   High — 2x supersampled: everything rasterized at 2x res, box-filtered
#include "resultsoverlay.h"
#include "resultsdoc.h"

#include <QPainter>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------
// Smooth palette generation: magenta → red → yellow → green → cyan
// ---------------------------------------------------------------
uint32_t ResultsOverlayRenderer::paletteColor(int level)
{
    double t = (double)level / (double)(kNumColors - 1);  // 0..1
    int r, g, b;
    if (t <= 0.5) {
        r = 255;
        g = (int)(255.0 * t * 2.0 + 0.5);
        b = (int)(255.0 * (1.0 - t * 2.0) + 0.5);
    } else {
        r = (int)(255.0 * (2.0 - t * 2.0) + 0.5);
        g = 255;
        b = (int)(255.0 * (t * 2.0 - 1.0) + 0.5);
    }
    r = std::max(0, std::min(255, r));
    g = std::max(0, std::min(255, g));
    b = std::max(0, std::min(255, b));
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// ---------------------------------------------------------------
// Gouraud-shaded triangle rasterizer — per-pixel color interpolation
// val0/val1/val2 are normalized color indices [0, kNumColors-1].
// ---------------------------------------------------------------
static inline void rasterTriangleGouraud(uint32_t *px, int stride, int W, int H,
                                          QPointF v0, QPointF v1, QPointF v2,
                                          double val0, double val1, double val2,
                                          const uint32_t *lut, int maxIdx)
{
    // Sort by Y
    if (v0.y() > v1.y()) { std::swap(v0, v1); std::swap(val0, val1); }
    if (v0.y() > v2.y()) { std::swap(v0, v2); std::swap(val0, val2); }
    if (v1.y() > v2.y()) { std::swap(v1, v2); std::swap(val1, val2); }

    double dyT = v2.y() - v0.y();
    if (dyT < 0.5) return;
    double invT = 1.0 / dyT;

    int y0 = std::max(0, (int)std::ceil(v0.y()));
    int y1 = std::min(H, (int)std::ceil(v1.y()));
    int y2 = std::min(H, (int)std::ceil(v2.y()));

    // Top half (v0 → v1)
    double dy01 = v1.y() - v0.y();
    if (dy01 > 0.001) {
        double inv01 = 1.0 / dy01;
        for (int y = y0; y < y1; y++) {
            double fy = (double)y - v0.y();
            double tA = fy * invT;
            double tB = fy * inv01;
            double xa = v0.x() + tA * (v2.x() - v0.x());
            double xb = v0.x() + tB * (v1.x() - v0.x());
            double va = val0 + tA * (val2 - val0);
            double vb = val0 + tB * (val1 - val0);
            if (xa > xb) { std::swap(xa, xb); std::swap(va, vb); }
            int xL = std::max(0, (int)std::ceil(xa));
            int xR = std::min(W, (int)std::ceil(xb));
            uint32_t *row = px + y * stride;
            double xSpan = xb - xa;
            if (xSpan > 0.001) {
                double invSpan = 1.0 / xSpan;
                for (int x = xL; x < xR; x++) {
                    double v = va + ((double)x - xa) * invSpan * (vb - va);
                    row[x] = lut[std::max(0, std::min(maxIdx, (int)(v + 0.5)))];
                }
            } else {
                uint32_t c = lut[std::max(0, std::min(maxIdx, (int)((va + vb) * 0.5 + 0.5)))];
                for (int x = xL; x < xR; x++) row[x] = c;
            }
        }
    }

    // Bottom half (v1 → v2)
    double dy12 = v2.y() - v1.y();
    if (dy12 > 0.001) {
        double inv12 = 1.0 / dy12;
        int ys = std::max(y0, y1);
        for (int y = ys; y < y2; y++) {
            double tA = ((double)y - v0.y()) * invT;
            double tB = ((double)y - v1.y()) * inv12;
            double xa = v0.x() + tA * (v2.x() - v0.x());
            double xb = v1.x() + tB * (v2.x() - v1.x());
            double va = val0 + tA * (val2 - val0);
            double vb = val1 + tB * (val2 - val1);
            if (xa > xb) { std::swap(xa, xb); std::swap(va, vb); }
            int xL = std::max(0, (int)std::ceil(xa));
            int xR = std::min(W, (int)std::ceil(xb));
            uint32_t *row = px + y * stride;
            double xSpan = xb - xa;
            if (xSpan > 0.001) {
                double invSpan = 1.0 / xSpan;
                for (int x = xL; x < xR; x++) {
                    double v = va + ((double)x - xa) * invSpan * (vb - va);
                    row[x] = lut[std::max(0, std::min(maxIdx, (int)(v + 0.5)))];
                }
            } else {
                uint32_t c = lut[std::max(0, std::min(maxIdx, (int)((va + vb) * 0.5 + 0.5)))];
                for (int x = xL; x < xR; x++) row[x] = c;
            }
        }
    }
}

// ---------------------------------------------------------------
// Bresenham line rasterizer — direct pixel writes
// ---------------------------------------------------------------
static inline void rasterLine(uint32_t *px, int stride, int W, int H,
                               int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        if ((unsigned)x0 < (unsigned)W && (unsigned)y0 < (unsigned)H)
            px[y0 * stride + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

// ---------------------------------------------------------------
// Wu's anti-aliased line rasterizer (Low mode)
// Blends line color over the existing pixel using alpha coverage.
// ---------------------------------------------------------------
static inline void plotAA(uint32_t *px, int stride, int W, int H,
                           int x, int y, uint32_t c, int alpha)
{
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H || alpha <= 0)
        return;
    if (alpha >= 255) { px[y * stride + x] = c; return; }

    uint32_t bg = px[y * stride + x];
    uint32_t a = (uint32_t)alpha;
    uint32_t ia = 255u - a;

    uint32_t rB = (bg >> 16) & 0xFF, gB = (bg >> 8) & 0xFF, bB = bg & 0xFF;
    uint32_t rF = (c >> 16) & 0xFF,  gF = (c >> 8) & 0xFF,  bF = c & 0xFF;

    uint32_t rO = (rF * a + rB * ia + 127) / 255;
    uint32_t gO = (gF * a + gB * ia + 127) / 255;
    uint32_t bO = (bF * a + bB * ia + 127) / 255;

    px[y * stride + x] = 0xFF000000u | (rO << 16) | (gO << 8) | bO;
}

static inline void rasterLineAA(uint32_t *px, int stride, int W, int H,
                                  double x0, double y0, double x1, double y1,
                                  uint32_t c)
{
    bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }

    double dx = x1 - x0, dy = y1 - y0;
    double gradient = (dx < 1e-10) ? 1.0 : dy / dx;

    // First endpoint
    double xEnd = std::round(x0);
    double yEnd = y0 + gradient * (xEnd - x0);
    double xGap = 1.0 - (x0 + 0.5 - std::floor(x0 + 0.5));
    int xpxl1 = (int)xEnd;
    int ypxl1 = (int)std::floor(yEnd);
    if (steep) {
        plotAA(px, stride, W, H, ypxl1,     xpxl1, c, (int)(255.0 * (1.0 - (yEnd - std::floor(yEnd))) * xGap));
        plotAA(px, stride, W, H, ypxl1 + 1, xpxl1, c, (int)(255.0 * (yEnd - std::floor(yEnd)) * xGap));
    } else {
        plotAA(px, stride, W, H, xpxl1, ypxl1,     c, (int)(255.0 * (1.0 - (yEnd - std::floor(yEnd))) * xGap));
        plotAA(px, stride, W, H, xpxl1, ypxl1 + 1, c, (int)(255.0 * (yEnd - std::floor(yEnd)) * xGap));
    }
    double intery = yEnd + gradient;

    // Second endpoint
    xEnd = std::round(x1);
    yEnd = y1 + gradient * (xEnd - x1);
    xGap = x1 + 0.5 - std::floor(x1 + 0.5);
    int xpxl2 = (int)xEnd;
    int ypxl2 = (int)std::floor(yEnd);
    if (steep) {
        plotAA(px, stride, W, H, ypxl2,     xpxl2, c, (int)(255.0 * (1.0 - (yEnd - std::floor(yEnd))) * xGap));
        plotAA(px, stride, W, H, ypxl2 + 1, xpxl2, c, (int)(255.0 * (yEnd - std::floor(yEnd)) * xGap));
    } else {
        plotAA(px, stride, W, H, xpxl2, ypxl2,     c, (int)(255.0 * (1.0 - (yEnd - std::floor(yEnd))) * xGap));
        plotAA(px, stride, W, H, xpxl2, ypxl2 + 1, c, (int)(255.0 * (yEnd - std::floor(yEnd)) * xGap));
    }

    // Main loop
    for (int x = xpxl1 + 1; x < xpxl2; x++) {
        int iy = (int)std::floor(intery);
        double frac = intery - (double)iy;
        int a1 = (int)(255.0 * (1.0 - frac));
        int a2 = (int)(255.0 * frac);
        if (steep) {
            plotAA(px, stride, W, H, iy,     x, c, a1);
            plotAA(px, stride, W, H, iy + 1, x, c, a2);
        } else {
            plotAA(px, stride, W, H, x, iy,     c, a1);
            plotAA(px, stride, W, H, x, iy + 1, c, a2);
        }
        intery += gradient;
    }
}

// ---------------------------------------------------------------
// Thick Wu's AA line — for SSAA modes. Each Wu pass covers ~2px,
// so we draw scale/2 passes with perpendicular offsets to get the
// right thickness for downsampling to a proper 1px AA line.
// ---------------------------------------------------------------
static inline void rasterLineAAScaled(uint32_t *px, int stride, int W, int H,
                                       double x0, double y0, double x1, double y1,
                                       uint32_t c, int scale)
{
    if (scale <= 2) {
        rasterLineAA(px, stride, W, H, x0, y0, x1, y1, c);
        return;
    }
    int passes = (scale + 1) / 2;
    bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
    double half = (double)(passes - 1) / 2.0;
    for (int d = 0; d < passes; d++) {
        double offset = (double)d - half;
        if (steep)
            rasterLineAA(px, stride, W, H, x0 + offset, y0, x1 + offset, y1, c);
        else
            rasterLineAA(px, stride, W, H, x0, y0 + offset, x1, y1 + offset, c);
    }
}

// ---------------------------------------------------------------
// Box-filter downsample: src (S*W x S*H) → dst (W x H)
// ---------------------------------------------------------------
static void downsampleNx(const uint32_t *src, int srcStride,
                          uint32_t *dst, int dstStride, int W, int H, int S)
{
    const int S2 = S * S;
    for (int y = 0; y < H; y++) {
        uint32_t *out = dst + y * dstStride;
        for (int x = 0; x < W; x++) {
            uint32_t aSum = 0, rSum = 0, gSum = 0, bSum = 0;
            for (int sy = 0; sy < S; sy++) {
                const uint32_t *row = src + (S * y + sy) * srcStride + S * x;
                for (int sx = 0; sx < S; sx++) {
                    uint32_t p = row[sx];
                    aSum += (p >> 24) & 0xFF;
                    rSum += (p >> 16) & 0xFF;
                    gSum += (p >> 8) & 0xFF;
                    bSum += p & 0xFF;
                }
            }
            uint32_t a = (aSum + S2 / 2) / S2;
            uint32_t r = (rSum + S2 / 2) / S2;
            uint32_t g = (gSum + S2 / 2) / S2;
            uint32_t b = (bSum + S2 / 2) / S2;
            out[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

// ---------------------------------------------------------------
ResultsOverlayRenderer::ResultsOverlayRenderer() = default;

QColor ResultsOverlayRenderer::colorForLevel(int level) const
{
    uint32_t c = m_colorLut[std::max(0, std::min(kNumColors - 1, level))];
    return QColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

double ResultsOverlayRenderer::getVertexValue(const SolnElement &elm, int vertex) const
{
    CmplxF b1, b2;
    if (m_doc->smoothB) { b1 = elm.b1[vertex]; b2 = elm.b2[vertex]; }
    else                { b1 = elm.B1;          b2 = elm.B2;          }

    switch (m_densityType) {
    case DensityType::B_mag:  return std::sqrt(std::norm(b1) + std::norm(b2));
    case DensityType::B_real: return std::sqrt(b1.real()*b1.real() + b2.real()*b2.real());
    case DensityType::B_imag: return std::sqrt(b1.imag()*b1.imag() + b2.imag()*b2.imag());
    default:                  return std::sqrt(std::norm(b1) + std::norm(b2));
    }
}

// ---------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------
void ResultsOverlayRenderer::render(QPainter &p, double ox, double oy,
                                     double mag, int widgetW, int widgetH)
{
    if (!m_doc) return;

    // Guard against empty/invalid ResultsDocuments (e.g. a stale overlay
    // set during a motion sweep whose document was freed or never loaded).
    if (m_doc->nodes.empty() || m_doc->elements.empty()) return;

    // Ensure smoothed B and plot bounds are computed (lazy, first render only)
    m_doc->ensureSmoothedB();

    bool needsImage = (m_densityType != DensityType::None) || m_showMesh || m_showContours;

    // Device pixel ratio: on Retina/HiDPI the physical pixel count is
    // dpr × the logical widget size.  We must rasterize at physical
    // resolution so the overlay is pixel-sharp, then tag the QImage with
    // the DPR so Qt blits it 1:1 to the backing store.
    const double dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const int physW = (int)(widgetW * dpr + 0.5);
    const int physH = (int)(widgetH * dpr + 0.5);

    // SSAA: High = 2x, Ultra = 4x, Extreme = 8x (on top of DPR)
    const bool ssaa = (m_aaQuality >= AAQuality::High) && needsImage;
    const int scale = (m_aaQuality == AAQuality::Extreme) ? 8
                    : (m_aaQuality == AAQuality::Ultra)   ? 4
                    : (m_aaQuality == AAQuality::High)    ? 2 : 1;
    m_scale = scale;
    const int rasterW = physW * scale;
    const int rasterH = physH * scale;
    const double rasterMag = mag * dpr * (double)scale;

    m_viewW = rasterW;
    m_viewH = rasterH;

    // Pre-compute screen coordinates for all nodes at raster resolution
    const size_t nNodes = m_doc->nodes.size();
    m_sc.resize(nNodes);
    const double hm1 = (double)(rasterH - 1);
    for (size_t i = 0; i < nNodes; i++) {
        m_sc[i].setX(rasterMag * (m_doc->nodes[i].x - ox));
        m_sc[i].setY(hm1 - rasterMag * (m_doc->nodes[i].y - oy));
    }

    if (needsImage) {
        // Resize/allocate raster image at physical resolution
        if (ssaa) {
            if (m_ssaaImage.width() != rasterW || m_ssaaImage.height() != rasterH)
                m_ssaaImage = QImage(rasterW, rasterH, QImage::Format_ARGB32_Premultiplied);
            m_ssaaImage.fill(0);
            m_px = (uint32_t *)m_ssaaImage.bits();
            m_stride = m_ssaaImage.bytesPerLine() / 4;
        } else {
            if (m_image.width() != physW || m_image.height() != physH)
                m_image = QImage(physW, physH, QImage::Format_ARGB32_Premultiplied);
            m_image.fill(0);
            m_px = (uint32_t *)m_image.bits();
            m_stride = m_image.bytesPerLine() / 4;
        }

        // Build 256-color smooth palette LUT
        for (int i = 0; i < kNumColors; i++)
            m_colorLut[i] = paletteColor(i);

        // Rasterize layers back-to-front into the pixel buffer
        if (m_densityType != DensityType::None)  rasterDensity();
        if (m_showMesh)                          rasterMesh();
        if (m_showContours)                      rasterContours();

        if (ssaa) {
            // Downsample SSAA buffer → final physical-res image
            if (m_image.width() != physW || m_image.height() != physH)
                m_image = QImage(physW, physH, QImage::Format_ARGB32_Premultiplied);
            m_image.fill(0);
            downsampleNx(m_px, m_stride,
                         (uint32_t *)m_image.bits(), m_image.bytesPerLine() / 4,
                         physW, physH, scale);
        }

        // Tag with DPR so Qt maps physical pixels 1:1 to the backing store
        m_image.setDevicePixelRatio(dpr);

        // Single blit — QPainter divides by DPR, so the image fills the widget
        bool wasAA = p.renderHints() & QPainter::Antialiasing;
        p.setRenderHint(QPainter::Antialiasing, false);
        p.drawImage(0, 0, m_image);
        p.setRenderHint(QPainter::Antialiasing, wasAA);
    }

    if (m_showLegend && m_densityType != DensityType::None)
        drawLegend(p, widgetW, widgetH);
}

// ---------------------------------------------------------------
// Density — Gouraud-shaded scanline rasterize each element
// ---------------------------------------------------------------
void ResultsOverlayRenderer::rasterDensity()
{
    for (int i = 0; i < (int)m_doc->elements.size(); i++) {
        const auto &elm = m_doc->elements[i];
        const QPointF &s0 = m_sc[elm.p[0]];
        const QPointF &s1 = m_sc[elm.p[1]];
        const QPointF &s2 = m_sc[elm.p[2]];

        // Bounding box cull
        double sxmin = std::min({s0.x(), s1.x(), s2.x()});
        double sxmax = std::max({s0.x(), s1.x(), s2.x()});
        double symin = std::min({s0.y(), s1.y(), s2.y()});
        double symax = std::max({s0.y(), s1.y(), s2.y()});
        if (sxmax < 0 || sxmin > m_viewW || symax < 0 || symin > m_viewH)
            continue;

        rasterElement(i);
    }
}

void ResultsOverlayRenderer::rasterElement(int elmIdx)
{
    const SolnElement &elm = m_doc->elements[elmIdx];
    const QPointF &sp0 = m_sc[elm.p[0]];
    const QPointF &sp1 = m_sc[elm.p[1]];
    const QPointF &sp2 = m_sc[elm.p[2]];

    double crossZ = (sp1.x()-sp0.x())*(sp2.y()-sp0.y())
                   - (sp2.x()-sp0.x())*(sp1.y()-sp0.y());
    if (std::fabs(crossZ) < 0.5) return;

    double bl = m_doc->B_Low, bh = m_doc->B_High;
    if (std::fabs(bh - bl) < 1e-30) bh = bl + 1.0;
    double scale = (double)(kNumColors - 1) / (bh - bl);

    // Compute normalized color indices for each vertex (0..kNumColors-1)
    // Level 0 = hot (magenta, high field), level kNumColors-1 = cold (cyan, low field)
    double bn[3];
    for (int j = 0; j < 3; j++) {
        double raw = getVertexValue(elm, j);
        double norm = (raw - bl) * scale;
        // Invert: high field = level 0 (magenta), low field = level 255 (cyan)
        bn[j] = (double)(kNumColors - 1) - std::max(0.0, std::min((double)(kNumColors - 1), norm));
    }

    rasterTriangleGouraud(m_px, m_stride, m_viewW, m_viewH,
                           sp0, sp1, sp2,
                           bn[0], bn[1], bn[2],
                           m_colorLut, kNumColors - 1);
}

// ---------------------------------------------------------------
// Contours — rasterize into pixel buffer
// ---------------------------------------------------------------
void ResultsOverlayRenderer::rasterContours()
{
    double aLow = m_doc->A_Low, aHigh = m_doc->A_High;
    double aRange = aHigh - aLow;
    if (aRange < 1e-30) return;

    int numC = m_numContours;
    double step = aRange / (double)numC;
    uint32_t black = 0xFF000000u;
    // Low mode: Wu's AA at native res. SSAA modes: thick Bresenham at supersample res.
    const bool ssaa = (m_scale > 1);

    for (int i = 0; i < (int)m_doc->elements.size(); i++) {
        const SolnElement &elm = m_doc->elements[i];
        const QPointF &s0 = m_sc[elm.p[0]];
        const QPointF &s1 = m_sc[elm.p[1]];
        const QPointF &s2 = m_sc[elm.p[2]];

        // Frustum cull
        double sxmin = std::min({s0.x(), s1.x(), s2.x()});
        double sxmax = std::max({s0.x(), s1.x(), s2.x()});
        double symin = std::min({s0.y(), s1.y(), s2.y()});
        double symax = std::max({s0.y(), s1.y(), s2.y()});
        if (sxmax < 0 || sxmin > m_viewW || symax < 0 || symin > m_viewH)
            continue;

        double a[3];
        for (int j = 0; j < 3; j++)
            a[j] = m_doc->nodes[elm.p[j]].A.real();

        // Only check contour levels that could cross this element
        double aMin = std::min({a[0], a[1], a[2]});
        double aMax = std::max({a[0], a[1], a[2]});
        int kMin = std::max(1, (int)std::ceil((aMin - aLow) / step));
        int kMax = std::min(numC - 1, (int)std::floor((aMax - aLow) / step));

        const QPointF *sp[3] = { &s0, &s1, &s2 };
        static const int edgeV[3][2] = {{0,1}, {1,2}, {2,0}};

        for (int k = kMin; k <= kMax; k++) {
            double level = aLow + step * (double)k;
            QPointF cp[2]; int nc = 0;

            for (int e = 0; e < 3 && nc < 2; e++) {
                int v0 = edgeV[e][0], v1 = edgeV[e][1];
                double diff = a[v1] - a[v0];
                if (std::fabs(diff) < 1e-30) continue;
                double t = (level - a[v0]) / diff;
                if (t < 0.0 || t > 1.0) continue;
                cp[nc++] = QPointF(
                    sp[v0]->x() + t * (sp[v1]->x() - sp[v0]->x()),
                    sp[v0]->y() + t * (sp[v1]->y() - sp[v0]->y()));
            }

            if (nc == 2) {
                if (ssaa)
                    rasterLineAAScaled(m_px, m_stride, m_viewW, m_viewH,
                                       cp[0].x(), cp[0].y(),
                                       cp[1].x(), cp[1].y(), black, m_scale);
                else if (m_aaQuality == AAQuality::Low)
                    rasterLineAA(m_px, m_stride, m_viewW, m_viewH,
                                 cp[0].x(), cp[0].y(),
                                 cp[1].x(), cp[1].y(), black);
                else
                    rasterLine(m_px, m_stride, m_viewW, m_viewH,
                               (int)(cp[0].x() + 0.5), (int)(cp[0].y() + 0.5),
                               (int)(cp[1].x() + 0.5), (int)(cp[1].y() + 0.5), black);
            }
        }
    }
}

// ---------------------------------------------------------------
// Mesh — rasterize into pixel buffer
// ---------------------------------------------------------------
void ResultsOverlayRenderer::rasterMesh()
{
    uint32_t meshColor = 0xFFA0A0A0u;  // grey, matching DrawingWidget::drawMesh
    const bool ssaa = (m_scale > 1);

    for (const auto &elm : m_doc->elements) {
        const QPointF &s0 = m_sc[elm.p[0]];
        const QPointF &s1 = m_sc[elm.p[1]];
        const QPointF &s2 = m_sc[elm.p[2]];

        double sxmin = std::min({s0.x(), s1.x(), s2.x()});
        double sxmax = std::max({s0.x(), s1.x(), s2.x()});
        double symin = std::min({s0.y(), s1.y(), s2.y()});
        double symax = std::max({s0.y(), s1.y(), s2.y()});
        if (sxmax < 0 || sxmin > m_viewW || symax < 0 || symin > m_viewH)
            continue;

        for (int e = 0; e < 3; e++) {
            int i0 = elm.p[e], i1 = elm.p[(e + 1) % 3];
            if (i0 > i1) continue;
            if (ssaa)
                rasterLineAAScaled(m_px, m_stride, m_viewW, m_viewH,
                                   m_sc[i0].x(), m_sc[i0].y(),
                                   m_sc[i1].x(), m_sc[i1].y(), meshColor, m_scale);
            else if (m_aaQuality == AAQuality::Low)
                rasterLineAA(m_px, m_stride, m_viewW, m_viewH,
                             m_sc[i0].x(), m_sc[i0].y(),
                             m_sc[i1].x(), m_sc[i1].y(), meshColor);
            else
                rasterLine(m_px, m_stride, m_viewW, m_viewH,
                           (int)(m_sc[i0].x() + 0.5), (int)(m_sc[i0].y() + 0.5),
                           (int)(m_sc[i1].x() + 0.5), (int)(m_sc[i1].y() + 0.5),
                           meshColor);
        }
    }
}

// ---------------------------------------------------------------
// Legend (still uses QPainter for text rendering)
// ---------------------------------------------------------------
void ResultsOverlayRenderer::drawLegend(QPainter &p, int w, int /*h*/)
{
    if (m_densityType == DensityType::None) return;

    int legendW = 30, legendH = 200, textW = 80, margin = 10;
    int x0 = w - legendW - textW - margin;
    int y0 = margin;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 200));
    p.drawRect(x0 - 5, y0 - 5, legendW + textW + 10, legendH + 30);

    p.setPen(Qt::black);
    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);

    QString title;
    switch (m_densityType) {
    case DensityType::B_mag:  title = "|B| (T)"; break;
    case DensityType::B_real: title = "|Re(B)| (T)"; break;
    case DensityType::B_imag: title = "|Im(B)| (T)"; break;
    case DensityType::H_mag:  title = "|H| (A/m)"; break;
    case DensityType::J_mag:  title = "|J| (A/m\u00b2)"; break;
    default: title = ""; break;
    }
    p.drawText(x0, y0, legendW + textW, 15, Qt::AlignCenter, title);

    double bl = m_doc->B_Low, bh = m_doc->B_High;
    f.setPointSize(7);
    p.setFont(f);

    // Draw smooth gradient bar using the full 256-color palette
    for (int i = 0; i < kNumColors; i++) {
        double yFrac = (double)i / (double)(kNumColors - 1);
        int y = y0 + 18 + (int)(yFrac * legendH);
        int yNext = y0 + 18 + (int)(((double)(i + 1) / (double)(kNumColors - 1)) * legendH);
        if (i == kNumColors - 1) yNext = y + 1;
        int bandH = std::max(1, yNext - y);
        uint32_t c = m_colorLut[i];
        p.setPen(Qt::NoPen);
        p.setBrush(QColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
        p.drawRect(x0, y, legendW, bandH);
    }

    // Draw value labels at regular intervals
    int numLabels = 6;  // 0%, 20%, 40%, 60%, 80%, 100%
    for (int i = 0; i <= numLabels; i++) {
        double frac = (double)i / (double)numLabels;
        double val = bh - frac * (bh - bl);
        int y = y0 + 18 + (int)(frac * legendH);
        p.setPen(Qt::black);
        p.drawText(x0 + legendW + 3, y - 5, textW - 3, 12,
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QString::number(val, 'e', 2));
    }
}
