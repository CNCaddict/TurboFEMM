#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <Accelerate/Accelerate.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "gpu_backend.h"
#include "metal_backend.h"

// SIMD-accelerated double→float conversion using vDSP
static inline void convertDoubleToFloat(const double* src, float* dst, int n)
{
    vDSP_vdpsp(src, 1, dst, 1, (vDSP_Length)n);
}

// SIMD-accelerated float→double conversion using vDSP
static inline void convertFloatToDouble(const float* src, double* dst, int n)
{
    vDSP_vspdp(src, 1, dst, 1, (vDSP_Length)n);
}

// Internal implementation using Objective-C Metal API
// All GPU buffers store float32 data. The public interface accepts double*
// from callers; conversion happens at upload/download boundaries.
struct MetalBackendImpl
{
    id<MTLDevice>              device;
    id<MTLCommandQueue>        queue;
    id<MTLLibrary>             library;

    // Compute pipeline states for each kernel
    id<MTLComputePipelineState> pso_spmv;
    id<MTLComputePipelineState> pso_jacobi;
    id<MTLComputePipelineState> pso_axpy;
    id<MTLComputePipelineState> pso_scal;
    id<MTLComputePipelineState> pso_copy;
    id<MTLComputePipelineState> pso_zero;
    id<MTLComputePipelineState> pso_dot_partial;
    id<MTLComputePipelineState> pso_dot_finalize;

    // Matrix buffers (CSR upper triangle) — float32 on GPU
    id<MTLBuffer> buf_diag;
    id<MTLBuffer> buf_row_ptr;
    id<MTLBuffer> buf_col_idx;
    id<MTLBuffer> buf_values;

    // Vector buffers (float32 on GPU)
    id<MTLBuffer> buf_V;
    id<MTLBuffer> buf_P;
    id<MTLBuffer> buf_R;
    id<MTLBuffer> buf_U;
    id<MTLBuffer> buf_Z;
    id<MTLBuffer> buf_b;

    // Scratch buffers for dot product reduction (float32)
    id<MTLBuffer> buf_partial_sums;
    id<MTLBuffer> buf_dot_result;

    // Stored original CPU double* pointers for identity resolution.
    // Operations receive the caller's double* and we match against these
    // to determine which GPU buffer to use.
    double* cpu_V;
    double* cpu_P;
    double* cpu_R;
    double* cpu_U;
    double* cpu_Z;
    double* cpu_b;

    int n;
    int nnz;
    int threadgroup_size;
    int num_threadgroups;

    // Batch mode state: when active, operations encode into batchCmd
    // instead of creating their own command buffers.
    bool batchMode;
    id<MTLCommandBuffer> batchCmd;

    bool initDevice()
    {
        device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "MetalBackend: No Metal device found\n");
            return false;
        }
        queue = [device newCommandQueue];

        // Load precompiled .metallib (fast — no runtime shader compilation).
        // Search order:
        //   1. Build directory metallib (FEMM_METALLIB_PATH from CMake)
        //   2. Next to executable (kernels.metallib)
        //   3. Bundle resource
        //   4. Fallback: compile from source (slow, ~500ms)

        NSError* error = nil;

#ifdef FEMM_METALLIB_PATH
        // Try CMake build-directory metallib first
        {
            NSString* buildLib = @FEMM_METALLIB_PATH;
            NSURL* url = [NSURL fileURLWithPath:buildLib];
            library = [device newLibraryWithURL:url error:&error];
            if (library) return true;
        }
#endif

        // Try next to the executable
        {
            NSString* execPath = [[NSProcessInfo processInfo] arguments][0];
            NSString* execDir = [execPath stringByDeletingLastPathComponent];
            NSString* libPath = [execDir stringByAppendingPathComponent:@"kernels.metallib"];
            NSURL* url = [NSURL fileURLWithPath:libPath];
            library = [device newLibraryWithURL:url error:nil];
            if (library) return true;
        }

        // Try bundle resource
        {
            NSString* path = [[NSBundle mainBundle] pathForResource:@"kernels" ofType:@"metallib"];
            if (path) {
                NSURL* url = [NSURL fileURLWithPath:path];
                library = [device newLibraryWithURL:url error:&error];
                if (library) return true;
            }
        }

        // Fallback: compile from source (slow, development only)
#ifdef FEMM_METAL_SHADER_PATH
        {
            NSString* srcPath = @FEMM_METAL_SHADER_PATH;
            NSString* src = [NSString stringWithContentsOfFile:srcPath
                                                     encoding:NSUTF8StringEncoding
                                                        error:&error];
            if (src) {
                MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
                library = [device newLibraryWithSource:src options:opts error:&error];
                if (library) {
                    fprintf(stderr, "MetalBackend: Using runtime shader compilation (slow)\n");
                    return true;
                }
                fprintf(stderr, "MetalBackend: Shader compile error: %s\n",
                        [[error localizedDescription] UTF8String]);
            } else {
                fprintf(stderr, "MetalBackend: Cannot load shaders: %s\n",
                        [[error localizedDescription] UTF8String]);
            }
        }
#endif

        fprintf(stderr, "MetalBackend: No shader library found\n");
        return false;
    }

    id<MTLComputePipelineState> makePSO(const char* name)
    {
        NSError* error = nil;
        id<MTLFunction> fn = [library newFunctionWithName:
                              [NSString stringWithUTF8String:name]];
        if (!fn) {
            fprintf(stderr, "MetalBackend: Function '%s' not found in shader library\n", name);
            return nil;
        }
        id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:fn
                                                                                error:&error];
        if (!pso) {
            fprintf(stderr, "MetalBackend: Pipeline error for '%s': %s\n",
                    name, [[error localizedDescription] UTF8String]);
        }
        return pso;
    }

    bool initPipelines()
    {
        pso_spmv         = makePSO("spmv_symmetric");
        pso_jacobi       = makePSO("jacobi_precond");
        pso_axpy         = makePSO("axpy_kernel");
        pso_scal         = makePSO("scal_kernel");
        pso_copy         = makePSO("copy_kernel");
        pso_zero         = makePSO("zero_kernel");
        pso_dot_partial  = makePSO("dot_partial");
        pso_dot_finalize = makePSO("dot_finalize");

        return (pso_spmv && pso_jacobi && pso_axpy && pso_scal &&
                pso_copy && pso_zero && pso_dot_partial && pso_dot_finalize);
    }

    // Create a shared-memory buffer (zero-copy on Apple Silicon)
    id<MTLBuffer> makeBuffer(size_t bytes)
    {
        return [device newBufferWithLength:bytes
                                  options:MTLResourceStorageModeShared];
    }

    // Create a shared-memory buffer initialized with data
    id<MTLBuffer> makeBuffer(const void* data, size_t bytes)
    {
        return [device newBufferWithBytes:data
                                  length:bytes
                                 options:MTLResourceStorageModeShared];
    }

    // Resolve a caller's double* to the corresponding GPU buffer.
    // Compares against the original CPU pointers stored at upload time.
    id<MTLBuffer> resolveBuffer(const double* ptr) const
    {
        if (ptr == cpu_V) return buf_V;
        if (ptr == cpu_P) return buf_P;
        if (ptr == cpu_R) return buf_R;
        if (ptr == cpu_U) return buf_U;
        if (ptr == cpu_Z) return buf_Z;
        if (ptr == cpu_b) return buf_b;
        return nil;
    }

    // Dispatch a compute kernel with n threads
    void dispatch(id<MTLComputeCommandEncoder> enc,
                  id<MTLComputePipelineState> pso, int count)
    {
        int tg = (int)pso.maxTotalThreadsPerThreadgroup;
        if (tg > 256) tg = 256;
        int groups = (count + tg - 1) / tg;
        [enc setComputePipelineState:pso];
        [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    }

    // Get or create the current command buffer.
    // In batch mode, reuses impl->batchCmd. Otherwise creates a new one.
    id<MTLCommandBuffer> getCmd()
    {
        if (batchMode) {
            if (!batchCmd)
                batchCmd = [queue commandBuffer];
            return batchCmd;
        }
        return [queue commandBuffer];
    }

    // Commit and wait. In batch mode, commits the shared buffer and resets.
    // In non-batch mode, commits the provided buffer.
    void commitAndWait(id<MTLCommandBuffer> cmd)
    {
        [cmd commit];
        [cmd waitUntilCompleted];
        if (batchMode && cmd == batchCmd) {
            batchCmd = nil;
        }
    }

    // Flush the batch command buffer (commit + wait) without ending batch mode.
    // Called by dot() when it needs to read a result from the GPU.
    void flushBatch()
    {
        if (batchCmd) {
            [batchCmd commit];
            [batchCmd waitUntilCompleted];
            batchCmd = nil;
        }
    }
};

// ============================================================
// MetalBackend public implementation
// ============================================================

MetalBackend::MetalBackend()
{
    impl = new MetalBackendImpl();
    impl->batchMode = false;
    impl->batchCmd = nil;
    impl->n = 0;
    if (!impl->initDevice() || !impl->initPipelines()) {
        fprintf(stderr, "MetalBackend: Initialization failed, falling back to CPU\n");
        delete impl;
        impl = nullptr;
    }
}

MetalBackend::~MetalBackend()
{
    if (impl) {
        // Flush any pending batch before destruction
        impl->flushBatch();
    }
    delete impl;
}

void MetalBackend::beginBatch()
{
    if (!impl) return;
    impl->batchMode = true;
    impl->batchCmd = nil;
}

void MetalBackend::endBatch()
{
    if (!impl) return;
    impl->flushBatch();
    impl->batchMode = false;
}

void MetalBackend::uploadMatrixReal(int n, int nnz,
                                    const double* diag,
                                    const int* row_ptr,
                                    const int* col_idx,
                                    const double* values)
{
    if (!impl) return;

    impl->n = n;
    impl->nnz = nnz;
    impl->threadgroup_size = 256;
    impl->num_threadgroups = (n + 255) / 256;

    // Convert double diag to float for GPU (SIMD-accelerated)
    std::vector<float> fdiag(n);
    convertDoubleToFloat(diag, fdiag.data(), n);
    impl->buf_diag = impl->makeBuffer(fdiag.data(), n * sizeof(float));

    // row_ptr and col_idx are int — no conversion needed
    impl->buf_row_ptr = impl->makeBuffer(row_ptr, (n+1) * sizeof(int));
    impl->buf_col_idx = impl->makeBuffer(col_idx, nnz * sizeof(int));

    // Convert double values to float for GPU (SIMD-accelerated)
    std::vector<float> fvalues(nnz);
    convertDoubleToFloat(values, fvalues.data(), nnz);
    impl->buf_values = impl->makeBuffer(fvalues.data(), nnz * sizeof(float));

    // Allocate dot product scratch buffers (float32)
    impl->buf_partial_sums = impl->makeBuffer(impl->num_threadgroups * sizeof(float));
    impl->buf_dot_result   = impl->makeBuffer(sizeof(float));
}

void MetalBackend::uploadVectors(int n,
                                 double* V, double* P, double* R,
                                 double* U, double* Z, double* b)
{
    if (!impl) return;

    // Store original CPU pointers for identity resolution
    impl->cpu_V = V;
    impl->cpu_P = P;
    impl->cpu_R = R;
    impl->cpu_U = U;
    impl->cpu_Z = Z;
    impl->cpu_b = b;

    // Convert each vector from double to float and create GPU buffers (SIMD-accelerated)
    std::vector<float> tmp(n);

    auto convertAndUpload = [&](const double* src) -> id<MTLBuffer> {
        convertDoubleToFloat(src, tmp.data(), n);
        return impl->makeBuffer(tmp.data(), n * sizeof(float));
    };

    impl->buf_V = convertAndUpload(V);
    impl->buf_P = convertAndUpload(P);
    impl->buf_R = convertAndUpload(R);
    impl->buf_U = convertAndUpload(U);
    impl->buf_Z = convertAndUpload(Z);
    impl->buf_b = convertAndUpload(b);
}

void MetalBackend::downloadSolution(int n, double* V)
{
    if (!impl) return;
    // Flush any pending batch before reading back
    impl->flushBatch();
    const float* gpu_data = (const float*)[impl->buf_V contents];
    convertFloatToDouble(gpu_data, V, n);
}

void MetalBackend::spmv(const double* X, double* Y)
{
    if (!impl) return;

    id<MTLBuffer> bufX = impl->resolveBuffer(X);
    id<MTLBuffer> bufY = impl->resolveBuffer(Y);
    if (!bufX || !bufY) return;

    int n = impl->n;
    id<MTLCommandBuffer> cmd = impl->getCmd();

    // First zero Y
    {
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:impl->pso_zero];
        [enc setBuffer:bufY offset:0 atIndex:0];
        [enc setBytes:&n length:sizeof(int) atIndex:1];
        int tg = 256;
        int groups = (n + tg - 1) / tg;
        [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
        [enc endEncoding];
    }

    // Then SpMV
    {
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:impl->pso_spmv];
        [enc setBuffer:impl->buf_diag    offset:0 atIndex:0];
        [enc setBuffer:impl->buf_row_ptr offset:0 atIndex:1];
        [enc setBuffer:impl->buf_col_idx offset:0 atIndex:2];
        [enc setBuffer:impl->buf_values  offset:0 atIndex:3];
        [enc setBuffer:bufX              offset:0 atIndex:4];
        [enc setBuffer:bufY              offset:0 atIndex:5];
        [enc setBytes:&n length:sizeof(int) atIndex:6];
        int tg = 256;
        int groups = (n + tg - 1) / tg;
        [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
        [enc endEncoding];
    }

    if (!impl->batchMode) {
        impl->commitAndWait(cmd);
    }
    // In batch mode, the encoders are deferred in batchCmd
}

void MetalBackend::precondJacobi(const double* X, double* Y)
{
    if (!impl) return;

    id<MTLBuffer> bufX = impl->resolveBuffer(X);
    id<MTLBuffer> bufY = impl->resolveBuffer(Y);
    if (!bufX || !bufY) return;

    int n = impl->n;
    id<MTLCommandBuffer> cmd = impl->getCmd();

    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:impl->pso_jacobi];
    [enc setBuffer:impl->buf_diag offset:0 atIndex:0];
    [enc setBuffer:bufX           offset:0 atIndex:1];
    [enc setBuffer:bufY           offset:0 atIndex:2];
    [enc setBytes:&n length:sizeof(int) atIndex:3];
    int tg = 256;
    int groups = (n + tg - 1) / tg;
    [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [enc endEncoding];

    if (!impl->batchMode) {
        impl->commitAndWait(cmd);
    }
}

double MetalBackend::dot(const double* X, const double* Y)
{
    if (!impl) return 0.0;

    id<MTLBuffer> bufX = impl->resolveBuffer(X);
    id<MTLBuffer> bufY = impl->resolveBuffer(Y);
    if (!bufX || !bufY) return 0.0;

    int n = impl->n;
    int ng = impl->num_threadgroups;

    // In batch mode, we encode the dot kernels into the shared command
    // buffer (which may already contain prior operations like spmv, axpy,
    // etc.), then flush — commit + wait — so we can read the scalar result.
    // This batches all preceding operations with the dot into one sync.
    id<MTLCommandBuffer> cmd = impl->getCmd();

    // Phase 1: partial sums per threadgroup
    {
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:impl->pso_dot_partial];
        [enc setBuffer:bufX                   offset:0 atIndex:0];
        [enc setBuffer:bufY                   offset:0 atIndex:1];
        [enc setBuffer:impl->buf_partial_sums offset:0 atIndex:2];
        [enc setBytes:&n length:sizeof(int) atIndex:3];
        int tg = 256;
        int groups = (n + tg - 1) / tg;
        [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
        [enc endEncoding];
    }

    // Phase 2: reduce partial sums to single value
    {
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:impl->pso_dot_finalize];
        [enc setBuffer:impl->buf_partial_sums offset:0 atIndex:0];
        [enc setBuffer:impl->buf_dot_result   offset:0 atIndex:1];
        [enc setBytes:&ng length:sizeof(int) atIndex:2];
        int tg2 = 256;
        [enc dispatchThreadgroups:MTLSizeMake(1, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(tg2, 1, 1)];
        [enc endEncoding];
    }

    // Always commit + wait here (need the scalar result on CPU).
    // In batch mode this also flushes all prior batched operations.
    impl->commitAndWait(cmd);

    // Read float32 result, return as double
    return (double)((float*)[impl->buf_dot_result contents])[0];
}

void MetalBackend::axpy(double alpha, const double* X, double* Y)
{
    if (!impl) return;

    id<MTLBuffer> bufX = impl->resolveBuffer(X);
    id<MTLBuffer> bufY = impl->resolveBuffer(Y);
    if (!bufX || !bufY) return;

    float falpha = (float)alpha;
    int n = impl->n;

    id<MTLCommandBuffer> cmd = impl->getCmd();
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:impl->pso_axpy];
    [enc setBuffer:bufX offset:0 atIndex:0];
    [enc setBuffer:bufY offset:0 atIndex:1];
    [enc setBytes:&falpha length:sizeof(float) atIndex:2];
    [enc setBytes:&n length:sizeof(int) atIndex:3];
    int tg = 256;
    int groups = (n + tg - 1) / tg;
    [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [enc endEncoding];

    if (!impl->batchMode) {
        impl->commitAndWait(cmd);
    }
}

void MetalBackend::scal(double alpha, double* Y)
{
    if (!impl) return;

    id<MTLBuffer> bufY = impl->resolveBuffer(Y);
    if (!bufY) return;

    float falpha = (float)alpha;
    int n = impl->n;

    id<MTLCommandBuffer> cmd = impl->getCmd();
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:impl->pso_scal];
    [enc setBuffer:bufY offset:0 atIndex:0];
    [enc setBytes:&falpha length:sizeof(float) atIndex:1];
    [enc setBytes:&n length:sizeof(int) atIndex:2];
    int tg = 256;
    int groups = (n + tg - 1) / tg;
    [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [enc endEncoding];

    if (!impl->batchMode) {
        impl->commitAndWait(cmd);
    }
}

void MetalBackend::copy(const double* X, double* Y)
{
    if (!impl) return;

    id<MTLBuffer> bufX = impl->resolveBuffer(X);
    id<MTLBuffer> bufY = impl->resolveBuffer(Y);
    if (!bufX || !bufY) return;

    int n = impl->n;

    id<MTLCommandBuffer> cmd = impl->getCmd();
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:impl->pso_copy];
    [enc setBuffer:bufX offset:0 atIndex:0];
    [enc setBuffer:bufY offset:0 atIndex:1];
    [enc setBytes:&n length:sizeof(int) atIndex:2];
    int tg = 256;
    int groups = (n + tg - 1) / tg;
    [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [enc endEncoding];

    if (!impl->batchMode) {
        impl->commitAndWait(cmd);
    }
}

void MetalBackend::zero(double* Y)
{
    if (!impl) return;

    id<MTLBuffer> bufY = impl->resolveBuffer(Y);
    if (!bufY) return;

    int n = impl->n;

    id<MTLCommandBuffer> cmd = impl->getCmd();
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:impl->pso_zero];
    [enc setBuffer:bufY offset:0 atIndex:0];
    [enc setBytes:&n length:sizeof(int) atIndex:1];
    int tg = 256;
    int groups = (n + tg - 1) / tg;
    [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [enc endEncoding];

    if (!impl->batchMode) {
        impl->commitAndWait(cmd);
    }
}

int MetalBackend::getN() const
{
    return impl ? impl->n : -1;
}

// ============================================================
// Factory: create the best available backend
// ============================================================
std::unique_ptr<GPUBackend> GPUBackend::create()
{
    auto metal = std::make_unique<MetalBackend>();
    // getN() returns -1 if impl is null (init failed), >= 0 otherwise
    if (metal->getN() >= 0) {
        return metal;
    }
    return nullptr;
}

#endif // __APPLE__
