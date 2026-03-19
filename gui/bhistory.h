// =============================================================
// B-field and Az history storage for iron loss computation
//
// During motion sweeps, stores per-element flux density and
// vector potential snapshots at each step.  Since the mesh is
// regenerated each step (element indices don't correspond),
// spatial lookup via grid-accelerated nearest-centroid is used
// to reconstruct B(t) and Az(t) at any point.
// =============================================================
#ifndef BHISTORY_H
#define BHISTORY_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>
#include <tuple>

// Compact per-step B-field + Az snapshot (loss-enabled elements only)
struct BSnapshot {
    std::vector<float> cx, cy;  // element centroids
    std::vector<float> bx, by;  // B components (Bx, By real part)
    std::vector<float> az;      // vector potential Az at centroid
    std::vector<int> label;     // block label index for label-aware lookup
    int numElements = 0;

    void reserve(int n) {
        cx.reserve(n);
        cy.reserve(n);
        bx.reserve(n);
        by.reserve(n);
        az.reserve(n);
        label.reserve(n);
    }

    void add(float centroidX, float centroidY, float Bx, float By,
             float Az = 0.0f, int blockLabel = -1) {
        cx.push_back(centroidX);
        cy.push_back(centroidY);
        bx.push_back(Bx);
        by.push_back(By);
        az.push_back(Az);
        label.push_back(blockLabel);
        numElements++;
    }

    void clear() {
        cx.clear(); cy.clear();
        bx.clear(); by.clear();
        az.clear();
        label.clear();
        numElements = 0;
    }
};

// Grid-accelerated spatial index for nearest-centroid lookup
class BHistoryIndex {
public:
    void build(const BSnapshot &snap) {
        m_snap = &snap;
        if (snap.numElements == 0) return;

        // Compute bounding box
        m_xMin = *std::min_element(snap.cx.begin(), snap.cx.end());
        m_yMin = *std::min_element(snap.cy.begin(), snap.cy.end());
        float xMax = *std::max_element(snap.cx.begin(), snap.cx.end());
        float yMax = *std::max_element(snap.cy.begin(), snap.cy.end());

        // Cell size: aim for ~20 elements per cell on average
        double area = (double)(xMax - m_xMin) * (double)(yMax - m_yMin);
        if (area < 1e-20) area = 1e-20;
        m_cellSize = std::sqrt(area * 20.0 / snap.numElements);
        if (m_cellSize < 1e-10) m_cellSize = 1e-10;

        m_nx = std::max(1, (int)std::ceil((xMax - m_xMin) / m_cellSize) + 1);
        m_ny = std::max(1, (int)std::ceil((yMax - m_yMin) / m_cellSize) + 1);

        // Build grid
        m_grid.clear();
        m_grid.resize((size_t)m_nx * m_ny);
        for (int i = 0; i < snap.numElements; i++) {
            int gx = (int)((snap.cx[i] - m_xMin) / m_cellSize);
            int gy = (int)((snap.cy[i] - m_yMin) / m_cellSize);
            gx = std::max(0, std::min(gx, m_nx - 1));
            gy = std::max(0, std::min(gy, m_ny - 1));
            m_grid[(size_t)gy * m_nx + gx].push_back(i);
        }
    }

    // Find nearest element index for a query point
    // Returns -1 if no data
    int findNearest(float qx, float qy, int labelFilter = -1) const {
        if (!m_snap || m_snap->numElements == 0)
            return -1;

        int gx = (int)((qx - m_xMin) / m_cellSize);
        int gy = (int)((qy - m_yMin) / m_cellSize);
        gx = std::max(0, std::min(gx, m_nx - 1));
        gy = std::max(0, std::min(gy, m_ny - 1));

        // Search the cell and its 8 neighbors
        float bestDist2 = std::numeric_limits<float>::max();
        int bestIdx = -1;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int cx = gx + dx;
                int cy = gy + dy;
                if (cx < 0 || cx >= m_nx || cy < 0 || cy >= m_ny)
                    continue;
                const auto &cell = m_grid[(size_t)cy * m_nx + cx];
                for (int idx : cell) {
                    if (labelFilter >= 0 && idx < (int)m_snap->label.size() &&
                        m_snap->label[idx] != labelFilter)
                        continue;
                    float ddx = m_snap->cx[idx] - qx;
                    float ddy = m_snap->cy[idx] - qy;
                    float d2 = ddx * ddx + ddy * ddy;
                    if (d2 < bestDist2) {
                        bestDist2 = d2;
                        bestIdx = idx;
                    }
                }
            }
        }

        if (bestIdx >= 0) return bestIdx;

        // Fallback: brute-force (shouldn't happen with proper grid sizing)
        for (int i = 0; i < m_snap->numElements; i++) {
            if (labelFilter >= 0 && i < (int)m_snap->label.size() &&
                m_snap->label[i] != labelFilter)
                continue;
            float ddx = m_snap->cx[i] - qx;
            float ddy = m_snap->cy[i] - qy;
            float d2 = ddx * ddx + ddy * ddy;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                bestIdx = i;
            }
        }
        return bestIdx;
    }

    // Lookup B at query point using nearest centroid
    // Returns (bx, by). If no data, returns (0, 0).
    std::pair<float, float> lookup(float qx, float qy) const {
        int idx = findNearest(qx, qy);
        if (idx >= 0)
            return {m_snap->bx[idx], m_snap->by[idx]};
        return {0.0f, 0.0f};
    }

    // Lookup Az at query point using nearest centroid
    float lookupAz(float qx, float qy) const {
        int idx = findNearest(qx, qy);
        if (idx >= 0)
            return m_snap->az[idx];
        return 0.0f;
    }

    // Lookup B and Az together (single spatial search)
    // Returns (bx, by, az)
    std::tuple<float, float, float> lookupAll(float qx, float qy,
                                              int labelFilter = -1) const {
        int idx = findNearest(qx, qy, labelFilter);
        if (idx < 0 && labelFilter >= 0)
            idx = findNearest(qx, qy);
        if (idx >= 0)
            return {m_snap->bx[idx], m_snap->by[idx], m_snap->az[idx]};
        return {0.0f, 0.0f, 0.0f};
    }

private:
    const BSnapshot *m_snap = nullptr;
    float m_xMin = 0.0f, m_yMin = 0.0f;
    double m_cellSize = 1.0;
    int m_nx = 0, m_ny = 0;
    std::vector<std::vector<int>> m_grid;
};

#endif // BHISTORY_H
