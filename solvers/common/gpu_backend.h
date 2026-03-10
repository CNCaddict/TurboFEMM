#ifndef GPU_BACKEND_H
#define GPU_BACKEND_H

#include <memory>
#include <cstddef>

// Abstract GPU backend interface for sparse linear algebra.
// Each platform provides an implementation: Metal (macOS), CUDA (NVIDIA), or CPU fallback.
// The solver calls these methods instead of raw loops, enabling GPU acceleration.
class GPUBackend
{
public:
    virtual ~GPUBackend() {}

    // Upload CSR symmetric matrix (upper triangle + diagonal separated)
    // After this call, the matrix data lives on GPU and SpMV/preconditioner can execute.
    //   diag[n]             - diagonal values
    //   row_ptr[n+1]        - CSR row pointers for off-diagonal entries
    //   col_idx[nnz_offdiag]- column indices
    //   values[nnz_offdiag] - off-diagonal values
    virtual void uploadMatrixReal(int n, int nnz,
                                  const double* diag,
                                  const int* row_ptr,
                                  const int* col_idx,
                                  const double* values) = 0;

    // Upload vectors to GPU (call before solve loop)
    // Returns opaque handles; all subsequent operations use GPU-resident data.
    virtual void uploadVectors(int n,
                               double* V, double* P, double* R,
                               double* U, double* Z, double* b) = 0;

    // Download solution vector from GPU back to CPU
    virtual void downloadSolution(int n, double* V) = 0;

    // Y = A * X  (symmetric CSR SpMV)
    virtual void spmv(const double* X, double* Y) = 0;

    // Y = M^{-1} * X  (Jacobi preconditioner: Y[i] = X[i] / diag[i])
    virtual void precondJacobi(const double* X, double* Y) = 0;

    // dot = X . Y  (returns scalar to CPU)
    virtual double dot(const double* X, const double* Y) = 0;

    // Y += alpha * X  (AXPY)
    virtual void axpy(double alpha, const double* X, double* Y) = 0;

    // Y = alpha * Y  (SCAL)
    virtual void scal(double alpha, double* Y) = 0;

    // Copy X into Y
    virtual void copy(const double* X, double* Y) = 0;

    // Zero a vector
    virtual void zero(double* Y) = 0;

    // Get problem dimension
    virtual int getN() const = 0;

    // Batch mode: reduce GPU-CPU sync points in iterative solvers.
    // When active, operations encode into a shared command buffer.
    // dot() automatically flushes (commits + waits) before reading results.
    // Non-dot operations are deferred until the next dot() or endBatch().
    virtual void beginBatch() {}
    virtual void endBatch() {}

    // Factory: create the best available backend for this platform
    static std::unique_ptr<GPUBackend> create();
};

#endif // GPU_BACKEND_H
