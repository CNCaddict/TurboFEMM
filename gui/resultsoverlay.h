// FEMM Qt 6 GUI — Shared results overlay renderer
// Used by both DrawingWidget (overlay mode) and ResultsWidget (standalone mode)
#ifndef RESULTSOVERLAY_H
#define RESULTSOVERLAY_H

#include "resultsdoc.h"
#include <QPainter>
#include <QImage>
#include <QColor>
#include <vector>

enum class AAQuality { None = 0, Low = 1, High = 2, Ultra = 3, Extreme = 4 };

class ResultsOverlayRenderer
{
public:
    static constexpr int kNumColors = 256;

    ResultsOverlayRenderer();

    void setDocument(ResultsDocument *doc) { m_doc = doc; }
    ResultsDocument *document() const { return m_doc; }

    void setShowDensity(DensityType type) { m_densityType = type; }
    void setShowContours(bool on) { m_showContours = on; }
    void setShowMesh(bool on) { m_showMesh = on; }
    void setShowLegend(bool on) { m_showLegend = on; }
    void setNumContours(int n) { m_numContours = n; }
    void setAAQuality(AAQuality q) { m_aaQuality = q; }

    // Manual color scale range override
    void setScaleRange(double lo, double hi) { m_scaleMin = lo; m_scaleMax = hi; }
    void setAutoScale(bool on) { m_autoScale = on; }
    bool autoScale() const { return m_autoScale; }
    double scaleMin() const { return m_scaleMin; }
    double scaleMax() const { return m_scaleMax; }

    // Compute percentile-based bounds from current data (clips outliers)
    // Returns {low, high} at the given percentile (e.g. 0.95 = 95th percentile)
    std::pair<double, double> computePercentileBounds(double percentile = 0.95) const;

    DensityType densityType() const { return m_densityType; }
    bool showContours() const { return m_showContours; }
    bool showMesh() const { return m_showMesh; }
    bool showLegend() const { return m_showLegend; }
    int numContours() const { return m_numContours; }
    AAQuality aaQuality() const { return m_aaQuality; }

    void render(QPainter &p, double ox, double oy, double mag,
                int widgetW, int widgetH);

    QColor colorForLevel(int level) const;

private:
    // Rasterize layers directly into m_image pixel buffer
    void rasterDensity();
    void rasterElement(int elmIdx, double bl, double bh);
    void rasterContours();
    void rasterMesh();
    void drawLegend(QPainter &p, int w, int h);

    double getVertexValue(const SolnElement &elm, int vertex) const;

    // Generate smooth magenta→yellow→cyan palette
    static uint32_t paletteColor(int level);

    ResultsDocument *m_doc = nullptr;
    DensityType m_densityType = DensityType::B_mag;
    bool m_showContours = true;
    bool m_showMesh = false;
    bool m_showLegend = true;
    int m_numContours = 19;
    AAQuality m_aaQuality = AAQuality::Low;

    // Pre-computed screen coordinates (populated at start of render())
    std::vector<QPointF> m_sc;
    int m_viewW = 0, m_viewH = 0;
    int m_scale = 1;  // SSAA scale factor (1, 2, or 4)

    // Shared pixel buffer for all overlay layers (reused across frames)
    QImage m_image;
    uint32_t *m_px = nullptr;
    int m_stride = 0;
    uint32_t m_colorLut[kNumColors];

    // Manual scale range (used when m_autoScale is false)
    bool m_autoScale = true;
    double m_scaleMin = 0.0;
    double m_scaleMax = 1.0;

    // 2x SSAA buffer (High quality mode)
    QImage m_ssaaImage;
};

#endif // RESULTSOVERLAY_H
