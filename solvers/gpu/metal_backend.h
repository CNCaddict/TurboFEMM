#ifndef METAL_BACKEND_H
#define METAL_BACKEND_H

#ifdef __APPLE__

#include "gpu_backend.h"

// Forward declare the Objective-C implementation (pimpl pattern)
struct MetalBackendImpl;

class MetalBackend : public GPUBackend
{
public:
    MetalBackend();
    ~MetalBackend() override;

    void uploadMatrixReal(int n, int nnz,
                          const double* diag,
                          const int* row_ptr,
                          const int* col_idx,
                          const double* values) override;

    void uploadVectors(int n,
                       double* V, double* P, double* R,
                       double* U, double* Z, double* b) override;

    void downloadSolution(int n, double* V) override;

    void spmv(const double* X, double* Y) override;
    double spmvDot(const double* X, double* Y) override;
    void precondJacobi(const double* X, double* Y) override;
    double precondJacobiDot(const double* X, double* Y) override;
    double dot(const double* X, const double* Y) override;
    void axpy(double alpha, const double* X, double* Y) override;
    void scal(double alpha, double* Y) override;
    void copy(const double* X, double* Y) override;
    void zero(double* Y) override;
    int getN() const override;

    void beginBatch() override;
    void endBatch() override;

private:
    MetalBackendImpl* impl;
};

#endif // __APPLE__
#endif // METAL_BACKEND_H
