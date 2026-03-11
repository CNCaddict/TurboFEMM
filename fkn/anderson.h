// =============================================================
// Anderson Acceleration for nonlinear FEM iteration
//
// Stores m previous iterates and finds the optimal linear
// combination via least-squares, typically cutting Newton
// iteration count by 2-3x on saturated magnetic materials.
//
// Works with both real (double*) and complex (CComplex*) vectors
// by treating complex arrays as interleaved double arrays of
// twice the length.
// =============================================================
#ifndef ANDERSON_H
#define ANDERSON_H

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>

class AndersonAccelerator
{
public:
    AndersonAccelerator() : m_depth(0), m_n(0), m_stored(0), m_col(0),
        m_f_prev(nullptr), m_g_prev(nullptr), m_dF(nullptr), m_dG(nullptr) {}

    ~AndersonAccelerator() { destroy(); }

    // Initialize with vector length n and history depth.
    // For complex problems, pass n = 2 * numComplexEntries.
    void init(int n, int depth = 5)
    {
        destroy();
        m_depth = depth;
        m_n = n;
        m_stored = 0;
        m_col = 0;

        m_f_prev = (double *)calloc(n, sizeof(double));
        m_g_prev = (double *)calloc(n, sizeof(double));

        m_dF = (double **)calloc(depth, sizeof(double *));
        m_dG = (double **)calloc(depth, sizeof(double *));
        for (int i = 0; i < depth; i++) {
            m_dF[i] = (double *)calloc(n, sizeof(double));
            m_dG[i] = (double *)calloc(n, sizeof(double));
        }
    }

    void destroy()
    {
        if (m_f_prev) { free(m_f_prev); m_f_prev = nullptr; }
        if (m_g_prev) { free(m_g_prev); m_g_prev = nullptr; }
        if (m_dF) {
            for (int i = 0; i < m_depth; i++) free(m_dF[i]);
            free(m_dF); m_dF = nullptr;
        }
        if (m_dG) {
            for (int i = 0; i < m_depth; i++) free(m_dG[i]);
            free(m_dG); m_dG = nullptr;
        }
        m_depth = 0;
        m_n = 0;
        m_stored = 0;
        m_col = 0;
    }

    // Clear history without freeing memory (e.g. after a restart).
    void reset()
    {
        m_stored = 0;
        m_col = 0;
    }

    // Apply Anderson acceleration.
    //
    //   x_old[n]  = iterate before the linear solve  (read-only)
    //   g_new[n]  = result of the linear solve G(x)   (modified in-place)
    //
    // Returns true if acceleration was applied.
    // Returns false if rejected (caller should use fallback relaxation).
    bool apply(const double *x_old, double *g_new)
    {
        if (!m_f_prev || m_n <= 0) return false;

        // f_k = g_k - x_k  (fixed-point residual)
        double *f_k = (double *)alloca(m_n * sizeof(double));
        for (int i = 0; i < m_n; i++)
            f_k[i] = g_new[i] - x_old[i];

        bool applied = false;

        if (m_stored > 0) {
            // Build differences: dF_j = f_k - f_prev  (for newest column)
            // and older columns already stored.
            // We need to add the current pair first.
            int col = m_col;
            for (int i = 0; i < m_n; i++) {
                m_dF[col][i] = f_k[i] - m_f_prev[i];
                m_dG[col][i] = g_new[i] - m_g_prev[i];
            }
            int ncols = (m_stored < m_depth) ? m_stored + 1 : m_depth;
            // The columns available are the last ncols entries in
            // the circular buffer ending at col (inclusive).

            // Map circular indices to a contiguous list
            int idx[16]; // m_depth <= 16
            for (int j = 0; j < ncols; j++) {
                idx[j] = (col - ncols + 1 + j + m_depth) % m_depth;
            }

            // Solve least-squares:  min ||f_k - dF * theta||^2
            // Normal equations:  (dF^T dF + eps*I) theta = dF^T f_k
            double FtF[64]; // max 8x8
            double Ftf[8];
            memset(FtF, 0, sizeof(double) * ncols * ncols);
            memset(Ftf, 0, sizeof(double) * ncols);

            for (int a = 0; a < ncols; a++) {
                for (int b = a; b < ncols; b++) {
                    double dot = 0.0;
                    for (int i = 0; i < m_n; i++)
                        dot += m_dF[idx[a]][i] * m_dF[idx[b]][i];
                    FtF[a * ncols + b] = dot;
                    FtF[b * ncols + a] = dot;
                }
                double dot = 0.0;
                for (int i = 0; i < m_n; i++)
                    dot += m_dF[idx[a]][i] * f_k[i];
                Ftf[a] = dot;
            }

            // Tikhonov regularization
            double trace = 0.0;
            for (int a = 0; a < ncols; a++)
                trace += FtF[a * ncols + a];
            double eps = 1.0e-12 * (trace > 0.0 ? trace : 1.0);
            for (int a = 0; a < ncols; a++)
                FtF[a * ncols + a] += eps;

            // Solve by Gaussian elimination with partial pivoting
            double theta[8];
            memcpy(theta, Ftf, sizeof(double) * ncols);
            bool solve_ok = gaussSolve(FtF, theta, ncols);

            if (solve_ok) {
                // Compute accelerated iterate:
                //   x_{k+1} = g_k - sum_j theta_j * dG_j
                // Store unmodified g_k for safeguard check
                double newton_norm2 = 0.0;
                for (int i = 0; i < m_n; i++)
                    newton_norm2 += f_k[i] * f_k[i];

                // Apply acceleration in a temporary buffer to check magnitude
                double accel_diff2 = 0.0;
                for (int i = 0; i < m_n; i++) {
                    double correction = 0.0;
                    for (int j = 0; j < ncols; j++)
                        correction += theta[j] * m_dG[idx[j]][i];
                    double new_val = g_new[i] - correction;
                    double diff = new_val - x_old[i];
                    accel_diff2 += diff * diff;
                }

                // Safeguard: reject if accelerated step is >10x Newton step
                if (accel_diff2 < 100.0 * newton_norm2 + 1.0e-30) {
                    for (int i = 0; i < m_n; i++) {
                        double correction = 0.0;
                        for (int j = 0; j < ncols; j++)
                            correction += theta[j] * m_dG[idx[j]][i];
                        g_new[i] -= correction;
                    }
                    applied = true;
                }
            }

            // Advance circular buffer
            m_col = (col + 1) % m_depth;
            if (m_stored < m_depth) m_stored++;
        } else {
            // First call — just store, no acceleration yet
            m_stored = 0; // will be incremented below
        }

        // Store current f_k and g_k for next iteration's differences
        // (g_new may have been modified by acceleration — store the
        //  UN-accelerated g_k for proper difference computation)
        if (!applied) {
            // g_new is still the raw Newton output
            memcpy(m_g_prev, g_new, m_n * sizeof(double));
        } else {
            // We need the raw g_k, which is x_old + f_k
            for (int i = 0; i < m_n; i++)
                m_g_prev[i] = x_old[i] + f_k[i];
        }
        memcpy(m_f_prev, f_k, m_n * sizeof(double));

        // If this was the very first call, mark that we have one stored now
        if (m_stored == 0) {
            m_stored = 1;
            m_col = 0;
        }

        return applied;
    }

private:
    int m_depth;
    int m_n;
    int m_stored;
    int m_col;

    double *m_f_prev;  // previous residual f_{k-1}
    double *m_g_prev;  // previous G(x_{k-1})
    double **m_dF;     // difference columns for residuals
    double **m_dG;     // difference columns for G outputs

    // Solve A*x = b in-place (A is ncols x ncols, b is ncols).
    // A is stored row-major in flat array. b is overwritten with solution.
    static bool gaussSolve(double *A, double *b, int n)
    {
        for (int k = 0; k < n; k++) {
            // Partial pivoting
            int pivot = k;
            double maxval = fabs(A[k * n + k]);
            for (int i = k + 1; i < n; i++) {
                double v = fabs(A[i * n + k]);
                if (v > maxval) { maxval = v; pivot = i; }
            }
            if (maxval < 1.0e-30) return false;

            if (pivot != k) {
                for (int j = k; j < n; j++) {
                    double tmp = A[k * n + j];
                    A[k * n + j] = A[pivot * n + j];
                    A[pivot * n + j] = tmp;
                }
                double tmp = b[k]; b[k] = b[pivot]; b[pivot] = tmp;
            }

            double inv = 1.0 / A[k * n + k];
            for (int i = k + 1; i < n; i++) {
                double factor = A[i * n + k] * inv;
                for (int j = k + 1; j < n; j++)
                    A[i * n + j] -= factor * A[k * n + j];
                b[i] -= factor * b[k];
            }
        }

        // Back-substitution
        for (int k = n - 1; k >= 0; k--) {
            for (int j = k + 1; j < n; j++)
                b[k] -= A[k * n + j] * b[j];
            b[k] /= A[k * n + k];
        }
        return true;
    }
};

#endif // ANDERSON_H
