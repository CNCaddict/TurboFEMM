#include <metal_stdlib>
using namespace metal;

// =============================================================
// FEMM GPU Compute Kernels for Metal
// All operate on single-precision (float32) data.
// The CPU-side backend converts double↔float at boundaries.
// =============================================================

// Full CSR SpMV: Y = A * X
// The Metal backend expands the solver's symmetric upper-triangular CSR
// into a full CSR structure at upload time so each GPU thread owns one row.
// This avoids atomic writes and removes the need for a separate zero pass.
kernel void spmv_symmetric(
    device const float* diag       [[buffer(0)]],   // diagonal [n]
    device const int*    row_ptr    [[buffer(1)]],   // CSR row pointers [n+1]
    device const int*    col_idx    [[buffer(2)]],   // column indices [nnz]
    device const float* values     [[buffer(3)]],   // off-diagonal values [nnz]
    device const float* X          [[buffer(4)]],   // input vector [n]
    device float* Y                [[buffer(5)]],   // output vector [n]
    constant int& n                 [[buffer(6)]],
    uint tid [[thread_position_in_grid]])
{
    if (tid >= (uint)n) return;

    int row = (int)tid;
    float sum = diag[row] * X[row];   // diagonal contribution

    int start = row_ptr[row];
    int end   = row_ptr[row + 1];

    for (int j = start; j < end; j++) {
        int col = col_idx[j];
        float val = values[j];
        sum += val * X[col];
    }

    Y[row] = sum;
}

// Full CSR SpMV with fused dot accumulation:
// computes Y = A * X and partial sums for X . Y.
kernel void spmv_dot_partial(
    device const float* diag       [[buffer(0)]],
    device const int*    row_ptr    [[buffer(1)]],
    device const int*    col_idx    [[buffer(2)]],
    device const float* values     [[buffer(3)]],
    device const float* X          [[buffer(4)]],
    device float* Y                [[buffer(5)]],
    device float* partial_sums     [[buffer(6)]],
    constant int& n                 [[buffer(7)]],
    uint tid    [[thread_position_in_grid]],
    uint lid    [[thread_position_in_threadgroup]],
    uint gid    [[threadgroup_position_in_grid]],
    uint tgSize [[threads_per_threadgroup]])
{
    threadgroup float shared[256];

    float dotVal = 0.0f;
    if (tid < (uint)n) {
        int row = (int)tid;
        float sum = diag[row] * X[row];
        int start = row_ptr[row];
        int end = row_ptr[row + 1];
        for (int j = start; j < end; j++) {
            sum += values[j] * X[col_idx[j]];
        }
        Y[row] = sum;
        dotVal = X[row] * sum;
    }
    shared[lid] = dotVal;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint stride = tgSize / 2; stride > 0; stride >>= 1) {
        if (lid < stride)
            shared[lid] += shared[lid + stride];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (lid == 0)
        partial_sums[gid] = shared[0];
}

// Jacobi preconditioner: Y[i] = X[i] / diag[i]
kernel void jacobi_precond(
    device const float* diag [[buffer(0)]],
    device const float* X    [[buffer(1)]],
    device float* Y          [[buffer(2)]],
    constant int& n           [[buffer(3)]],
    uint tid [[thread_position_in_grid]])
{
    if (tid >= (uint)n) return;
    Y[tid] = X[tid] / diag[tid];
}

// Jacobi preconditioner with fused dot accumulation:
// computes Y = M^{-1} * X and partial sums for X . Y.
kernel void jacobi_precond_dot_partial(
    device const float* diag       [[buffer(0)]],
    device const float* X          [[buffer(1)]],
    device float* Y                [[buffer(2)]],
    device float* partial_sums     [[buffer(3)]],
    constant int& n                 [[buffer(4)]],
    uint tid    [[thread_position_in_grid]],
    uint lid    [[thread_position_in_threadgroup]],
    uint gid    [[threadgroup_position_in_grid]],
    uint tgSize [[threads_per_threadgroup]])
{
    threadgroup float shared[256];

    float dotVal = 0.0f;
    if (tid < (uint)n) {
        float y = X[tid] / diag[tid];
        Y[tid] = y;
        dotVal = X[tid] * y;
    }
    shared[lid] = dotVal;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint stride = tgSize / 2; stride > 0; stride >>= 1) {
        if (lid < stride)
            shared[lid] += shared[lid + stride];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (lid == 0)
        partial_sums[gid] = shared[0];
}

// AXPY: Y += alpha * X
kernel void axpy_kernel(
    device const float* X    [[buffer(0)]],
    device float* Y          [[buffer(1)]],
    constant float& alpha    [[buffer(2)]],
    constant int& n           [[buffer(3)]],
    uint tid [[thread_position_in_grid]])
{
    if (tid >= (uint)n) return;
    Y[tid] += alpha * X[tid];
}

// SCAL: Y *= alpha
kernel void scal_kernel(
    device float* Y          [[buffer(0)]],
    constant float& alpha    [[buffer(1)]],
    constant int& n           [[buffer(2)]],
    uint tid [[thread_position_in_grid]])
{
    if (tid >= (uint)n) return;
    Y[tid] *= alpha;
}

// COPY: Y = X
kernel void copy_kernel(
    device const float* X [[buffer(0)]],
    device float* Y       [[buffer(1)]],
    constant int& n        [[buffer(2)]],
    uint tid [[thread_position_in_grid]])
{
    if (tid >= (uint)n) return;
    Y[tid] = X[tid];
}

// ZERO: Y = 0
kernel void zero_kernel(
    device float* Y    [[buffer(0)]],
    constant int& n     [[buffer(1)]],
    uint tid [[thread_position_in_grid]])
{
    if (tid >= (uint)n) return;
    Y[tid] = 0.0f;
}

// Dot product — Phase 1: per-threadgroup partial sums
// Each threadgroup reduces its portion to a single partial sum.
kernel void dot_partial(
    device const float* X        [[buffer(0)]],
    device const float* Y        [[buffer(1)]],
    device float* partial_sums   [[buffer(2)]],
    constant int& n               [[buffer(3)]],
    uint tid    [[thread_position_in_grid]],
    uint lid    [[thread_position_in_threadgroup]],
    uint gid    [[threadgroup_position_in_grid]],
    uint tgSize [[threads_per_threadgroup]])
{
    // Shared memory for threadgroup reduction
    threadgroup float shared[256];

    float val = 0.0f;
    if (tid < (uint)n) {
        val = X[tid] * Y[tid];
    }
    shared[lid] = val;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Tree reduction within threadgroup
    for (uint stride = tgSize / 2; stride > 0; stride >>= 1) {
        if (lid < stride) {
            shared[lid] += shared[lid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    // First thread writes the threadgroup's partial sum
    if (lid == 0) {
        partial_sums[gid] = shared[0];
    }
}

// Dot product — Phase 2: reduce partial sums to final scalar
kernel void dot_finalize(
    device float* partial_sums   [[buffer(0)]],
    device float* result         [[buffer(1)]],
    constant int& num_groups      [[buffer(2)]],
    uint lid    [[thread_position_in_threadgroup]],
    uint tgSize [[threads_per_threadgroup]])
{
    threadgroup float shared[256];

    float val = 0.0f;
    if (lid < (uint)num_groups) {
        val = partial_sums[lid];
    }
    shared[lid] = val;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint stride = tgSize / 2; stride > 0; stride >>= 1) {
        if (lid < stride) {
            shared[lid] += shared[lid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (lid == 0) {
        result[0] = shared[0];
    }
}
