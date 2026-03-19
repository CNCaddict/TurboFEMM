#include <stdafx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"

#ifndef _WIN32
#include <sys/time.h>
static double pcg_wallclock_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

#ifdef FEMM_USE_BLAS
  #ifdef __APPLE__
    #include <Accelerate/Accelerate.h>
  #else
    #include <cblas.h>
  #endif
#endif

#ifdef FEMM_USE_METAL
  #include "gpu_backend.h"
#endif

#define KLUDGE

CEntry::CEntry()
{
	next=NULL;
	x=0;
	c=0;
}

// ============================================================
// CEntryPool — block allocator for CEntry nodes
// Eliminates millions of individual new/delete per solve.
// All entries live in contiguous 64K-entry blocks for cache locality.
// ============================================================

CEntryPool::CEntryPool()
{
	blocks = NULL;
	numBlocks = 0;
	allocBlocks = 0;
	maxBlocks = 0;
	nextInBlock = BLOCK_SIZE; // force first alloc() to create a block
}

CEntryPool::~CEntryPool()
{
	for (int i = 0; i < allocBlocks; i++)
		delete blocks[i];
	free(blocks);
}

void CEntryPool::addBlock()
{
	if (numBlocks < allocBlocks) {
		// Reuse a previously allocated block (after reset)
		numBlocks++;
		nextInBlock = 0;
		return;
	}
	// Need a genuinely new block
	if (allocBlocks >= maxBlocks) {
		maxBlocks = maxBlocks ? maxBlocks * 2 : 8;
		blocks = (Block **)realloc(blocks, maxBlocks * sizeof(Block *));
	}
	blocks[allocBlocks] = new Block();
	allocBlocks++;
	numBlocks = allocBlocks;
	nextInBlock = 0;
}

CEntry *CEntryPool::alloc()
{
	if (nextInBlock >= BLOCK_SIZE)
		addBlock();
	CEntry *e = &blocks[numBlocks - 1]->entries[nextInBlock++];
	e->x = 0;
	e->c = 0;
	e->next = NULL;
	return e;
}

void CEntryPool::reset()
{
	// Keep blocks allocated but reset cursor to reuse them.
	// Avoids re-allocating on subsequent Newton iterations.
	numBlocks = 0;
	nextInBlock = BLOCK_SIZE;
}

CBigLinProb::CBigLinProb()
{
	n=0;
	csrD=NULL;
	csrR=NULL;
	csrC=NULL;
	csrV=NULL;
	csrNnz=0;
	csrReady=0;
#ifdef FEMM_USE_METAL
	externalGPU=NULL;
#endif
}

CBigLinProb::~CBigLinProb()
{
	if (n==0) return;

	free(b); free(P); free(R);
	free(V); free(U); free(Z);

	// All CEntry nodes (diagonal + off-diagonal) live in the pool.
	// The pool destructor frees them in bulk — no per-entry delete.
	free(M);

	// free CSR arrays
	free(csrD);
	free(csrR);
	free(csrC);
	free(csrV);
}

int CBigLinProb::Create(int d, int bw)
{
	int i;

	bdw=bw;
	b=(double *)calloc(d,sizeof(double));
	V=(double *)calloc(d,sizeof(double));
	P=(double *)calloc(d,sizeof(double));
	R=(double *)calloc(d,sizeof(double));
	U=(double *)calloc(d,sizeof(double));
	Z=(double *)calloc(d,sizeof(double));

	M=(CEntry **)calloc(d,sizeof(CEntry *));
	n=d;

	// Diagonal entries allocated from pool (contiguous, cache-friendly)
	for(i=0;i<d;i++){
		M[i] = entryPool.alloc();
		M[i]->c = i;
	}

	csrD=NULL;
	csrR=NULL;
	csrC=NULL;
	csrV=NULL;
	csrNnz=0;
	csrReady=0;

	return 1;
}

void CBigLinProb::Put(double v, int p, int q)
{
	CEntry *e,*l;
	int i;

	if(q<p){ i=p; p=q; q=i; }

	e=M[p];

	while((e->c < q) && (e->next != NULL))
	{
		l=e;
		e=e->next;
		// Prefetch next node to hide memory latency
		if(e->next) __builtin_prefetch(e->next, 0, 1);
	}

	if(e->c == q){
		e->x=v;
		return;
	}

	CEntry *m = entryPool.alloc();

	if((e->next == NULL) && (q > e->c)){
		e->next = m;
		m->c = q;
		m->x = v;
	}
	else{
		l->next=m;
		m->next=e;
		m->c=q;
		m->x=v;
	}
	return;
}

double CBigLinProb::Get(int p, int q)
{
	CEntry *e;

	if(q<p){ int i; i=p; p=q; q=i; }

	e=M[p];

	while((e->c < q) && (e->next != NULL)) e=e->next;

	if(e->c == q) return e->x;

	return 0;
}

void CBigLinProb::AddTo(double v, int p, int q)
{
	// Single-pass find-and-add (replaces double-traversal Put(Get()+v))
	CEntry *e, *l;
	int i;

	if(q < p){ i = p; p = q; q = i; }

	l = NULL;
	e = M[p];

	while((e->c < q) && (e->next != NULL)){
		l = e;
		e = e->next;
		// Prefetch next node to hide memory latency
		if(e->next) __builtin_prefetch(e->next, 0, 1);
	}

	if(e->c == q){
		e->x += v;
		return;
	}

	// Entry doesn't exist yet — allocate from pool
	CEntry *m = entryPool.alloc();
	m->c = q;
	m->x = v;

	if((e->next == NULL) && (q > e->c)){
		e->next = m;
	}
	else{
		l->next = m;
		m->next = e;
	}
}

// Convert linked-list storage to CSR format.
// Stores upper triangle only (col >= row), with diagonal separated.
// Must be called after assembly and before the iterative solve.
void CBigLinProb::Finalize()
{
	int i;
	CEntry *e;

	// Count off-diagonal entries
	csrNnz = 0;
	for(i=0; i<n; i++){
		e = M[i]->next;  // skip diagonal (first entry)
		while(e != NULL){
			csrNnz++;
			e = e->next;
		}
	}

	// Allocate CSR arrays
	free(csrD); free(csrR); free(csrC); free(csrV);
	csrD = (double *)malloc(n * sizeof(double));
	csrR = (int *)malloc((n+1) * sizeof(int));
	csrC = (int *)malloc(csrNnz * sizeof(int));
	csrV = (double *)malloc(csrNnz * sizeof(double));

	// Fill CSR arrays from linked lists
	int pos = 0;
	for(i=0; i<n; i++){
		csrD[i] = M[i]->x;			// diagonal value
		csrR[i] = pos;
		e = M[i]->next;			// off-diagonal entries
		while(e != NULL){
			csrC[pos] = e->c;
			csrV[pos] = e->x;
			pos++;
			e = e->next;
		}
	}
	csrR[n] = pos;

	csrReady = 1;
}

// Symmetric sparse matrix-vector product Y = A * X
// Uses CSR upper-triangle storage, exploiting symmetry.
void CBigLinProb::MultA(double *X, double *Y)
{
	int i, j, c;
	double v;

	for(i=0; i<n; i++) Y[i] = 0;

	if(csrReady){
		// CSR path: contiguous memory, cache-friendly
		for(i=0; i<n; i++){
			Y[i] += csrD[i] * X[i];		// diagonal
			for(j=csrR[i]; j<csrR[i+1]; j++){
				c = csrC[j];
				v = csrV[j];
				Y[i] += v * X[c];			// upper triangle
				Y[c] += v * X[i];			// lower triangle (symmetry)
			}
		}
	}
	else{
		// Linked-list fallback (used before Finalize)
		CEntry *e;
		for(i=0; i<n; i++){
			Y[i] += M[i]->x * X[i];
			e = M[i]->next;
			while(e != NULL){
				Y[i] += e->x * X[e->c];
				Y[e->c] += e->x * X[i];
				e = e->next;
			}
		}
	}
}

double CBigLinProb::Dot(double *X, double *Y)
{
#ifdef FEMM_USE_BLAS
	return cblas_ddot(n, X, 1, Y, 1);
#else
	int i;
	double z;

	for(i=0,z=0;i<n;i++) z+=X[i]*Y[i];

	return z;
#endif
}

// Preconditioner: Y = M^{-1} * X
// Default is Jacobi (diagonal scaling) — trivially parallel, GPU-friendly.
// SSOR is available as a fallback via FEMM_USE_SSOR_PRECOND.
void CBigLinProb::MultPC(double *X, double *Y)
{
	int i, j;

#ifndef FEMM_USE_SSOR_PRECOND
	// Jacobi preconditioner: Y[i] = X[i] / diag[i]
	// Fully parallel — maps directly to GPU compute shader
	if(csrReady){
		for(i=0; i<n; i++) Y[i] = X[i] / csrD[i];
	}
	else{
		for(i=0; i<n; i++) Y[i] = X[i] / M[i]->x;
	}
#else
	// SSOR preconditioner (sequential triangular solves, CPU only)
	double lam = LAMBDA;

	if(csrReady){
		double s = lam * (2. - lam);
		for(i=0; i<n; i++) Y[i] = X[i] * s;

		// Forward sweep (lower triangle solve)
		for(i=0; i<n; i++){
			Y[i] /= csrD[i];
			for(j=csrR[i]; j<csrR[i+1]; j++){
				Y[csrC[j]] -= csrV[j] * Y[i] * lam;
			}
		}

		// Scale by diagonal
		for(i=0; i<n; i++) Y[i] *= csrD[i];

		// Backward sweep (upper triangle solve)
		for(i=n-1; i>=0; i--){
			for(j=csrR[i]; j<csrR[i+1]; j++){
				Y[i] -= csrV[j] * Y[csrC[j]] * lam;
			}
			Y[i] /= csrD[i];
		}
	}
	else{
		CEntry *e;
		double cc = LAMBDA*(2.-LAMBDA);
		for(i=0;i<n;i++) Y[i]=X[i]*cc;

		for(i=0;i<n;i++){
			Y[i]/= M[i]->x;
			e=M[i]->next;
			while(e!=NULL){
				Y[e->c] -= e->x * Y[i] * LAMBDA;
				e=e->next;
			}
		}
		for(i=0;i<n;i++) Y[i]*=M[i]->x;
		for(i=n-1;i>=0;i--){
			e=M[i]->next;
			while(e!=NULL){
				Y[i] -= e->x * Y[e->c] * LAMBDA;
				e=e->next;
			}
			Y[i]/= M[i]->x;
		}
	}
#endif
}

int CBigLinProb::PCGSolve(int flag)
{
	int i;
	double res,res_o,res_new;
	double er,del,rho,pAp;

	// quick check for most obvious sign of singularity;
	for(i=0;i<n;i++) if(M[i]->x==0){
		fprintf(stderr,"singular flag tripped.");
		return 0;
	}

	// Convert linked-list to CSR before iterating
#ifndef _WIN32
	double pcg_t0 = pcg_wallclock_ms(), pcg_t1;
#endif
	Finalize();
#ifndef _WIN32
	pcg_t1 = pcg_wallclock_ms();
	// fprintf(stderr, "[fkn]   Finalize (CSR): %.1f ms\n", pcg_t1-pcg_t0);
	pcg_t0 = pcg_t1;
#endif

#ifdef FEMM_USE_METAL
	// ============================================================
	// GPU-accelerated PCG via Metal (Apple Silicon)
	// Uses float32 for speed.  Falls back to CPU double-precision
	// path if the GPU solver diverges or stalls (can happen with
	// very fine meshes where float32 precision is insufficient).
	// ============================================================
	bool gpuSolveOk = false;
	if(n >= 1000)  // GPU overhead only worthwhile for larger problems
	{
		// Use persistent GPU backend if available (saves ~15ms pipeline init)
		std::unique_ptr<GPUBackend> ownGpu;
		GPUBackend *gpu;
		if(externalGPU) {
			gpu = externalGPU;
		} else {
			ownGpu = GPUBackend::create();
			gpu = ownGpu.get();
		}
#ifndef _WIN32
		pcg_t1 = pcg_wallclock_ms();
		pcg_t0 = pcg_t1;
#endif
		if(gpu)
		{
			// Upload CSR matrix to GPU
			gpu->uploadMatrixReal(n, csrNnz, csrD, csrR, csrC, csrV);

			// Upload vectors to GPU (shared memory = zero copy on Apple Silicon)
			gpu->uploadVectors(n, V, P, R, U, Z, b);

			// initialize progress bar;
			TheView->SetDlgItemText(IDC_FRAME1,"GPU Conjugate Gradient Solver");
			TheView->m_prog1.SetPos(0);
			int prg1=0, prg2;

			// Enable batch mode: operations are batched into shared command
			// buffers, flushing only when dot() needs a scalar result.
			gpu->beginBatch();

			// residual with V=0: Z = M^{-1} * b
			res_o = gpu->precondJacobiDot(b, Z);  // flush + read
			if(res_o==0) { gpu->endBatch(); gpu->downloadSolution(n, V); return 1; }

			// if flag is false, initialize V with zeros
			if(flag==0) gpu->zero(V);

			// form residual: R = b - A*V
			gpu->spmv(V, R);  // R = A*V  (batched)
			gpu->scal(-1.0, R);               // batched
			gpu->axpy(1.0, b, R);             // batched: R = b - A*V

			// form initial search direction: Z = M^{-1}*R, P = Z
			res = gpu->precondJacobiDot(R, Z); // flush + read
			gpu->copy(Z, P);                   // batched

			// PCG iteration loop — batched GPU operations
			int maxPcgIter = (n > 10000) ? n : 10 * n;
			if (maxPcgIter < 1000) maxPcgIter = 1000;
			int pcgIter = 0;
			bool gpuDiverged = false;

			do{
				pAp = gpu->spmvDot(P, U);  // U = A * P and pAp = P.U
				del = res / pAp;

				gpu->axpy(del, P, V);      // V += del * P  (batched)
				gpu->axpy(-del, U, R);     // R -= del * U  (batched)
				res_new = gpu->precondJacobiDot(R, Z);  // Z = M^{-1} * R and res_new = Z.R
				rho = res_new / res;
				res = res_new;

				gpu->scal(rho, P);         // P *= rho  (batched)
				gpu->axpy(1.0, Z, P);      // P += Z    (batched)

				er = sqrt(res / res_o);
				pcgIter++;

				// NaN / divergence detection
				if (er != er || er > 1e10) {
					fprintf(stderr, "[fkn] GPU PCG diverged at iter %d: er=%g (n=%d) — will retry on CPU\n",
							pcgIter, er, n);
					gpuDiverged = true;
					break;
				}

				prg2 = (int)(20. * log10(er) / (log10(Precision)));
				if(prg2 > prg1){
					prg1 = prg2;
					prg2 = (prg1 * 5);
					if(prg2 > 100) prg2 = 100;
					TheView->m_prog1.SetPos(prg2);
					TheView->InvalidateRect(NULL, FALSE);
					TheView->UpdateWindow();
				}
			} while(er > Precision && pcgIter < maxPcgIter);

			gpu->endBatch();

			if (!gpuDiverged && er <= Precision) {
				// GPU converged successfully
				gpu->downloadSolution(n, V);
				gpuSolveOk = true;
			} else if (!gpuDiverged) {
				fprintf(stderr, "[fkn] GPU PCG stalled: er=%g after %d/%d iters (n=%d) — will retry on CPU\n",
						er, pcgIter, maxPcgIter, n);
			}
			// If !gpuSolveOk, fall through to CPU path below
		}
	}
	if (gpuSolveOk) return 1;
#endif // FEMM_USE_METAL

	// ============================================================
	// CPU PCG path (double precision — BLAS-accelerated when available)
	// Also serves as fallback when GPU float32 PCG diverges.
	// ============================================================

	// initialize progress bar;
	TheView->SetDlgItemText(IDC_FRAME1,"Conjugate Gradient Solver");
	TheView->m_prog1.SetPos(0);
	int prg1=0;
	int prg2;

	// residual with V=0
	MultPC(b,Z);
	res_o=Dot(Z,b);
	if(res_o==0) return 1;

	// if flag is false, initialize V with zeros;
	if (flag==0) for(i=0;i<n;i++) V[i]=0;

	// form residual;
	MultA(V,R);
	for(i=0;i<n;i++) R[i]=b[i]-R[i];

	// form initial search direction;
	MultPC(R,Z);
	for(i=0;i<n;i++) P[i]=Z[i];
	res=Dot(Z,R);

	// do iteration;
	int maxPcgIter = (n > 10000) ? n : 10 * n;
	if (maxPcgIter < 1000) maxPcgIter = 1000;
	int pcgIter = 0;

	do{
		// step i)
		MultA(P,U);
		pAp=Dot(P,U);
		del=res/pAp;

		// step ii) V += del * P
#ifdef FEMM_USE_BLAS
		cblas_daxpy(n, del, P, 1, V, 1);
#else
		for(i=0;i<n;i++) V[i]+=(del*P[i]);
#endif

		// step iii) R -= del * U
#ifdef FEMM_USE_BLAS
		cblas_daxpy(n, -del, U, 1, R, 1);
#else
		for(i=0;i<n;i++) R[i]-=(del*U[i]);
#endif

		// step iv)
		MultPC(R,Z);
		res_new=Dot(Z,R);
		rho=res_new/res;
		res=res_new;

		// step v) P = Z + rho * P
#ifdef FEMM_USE_BLAS
		cblas_dscal(n, rho, P, 1);
		cblas_daxpy(n, 1.0, Z, 1, P, 1);
#else
		for(i=0;i<n;i++) P[i]=Z[i]+(rho*P[i]);
#endif

		// have we converged yet?
		er=sqrt(res/res_o);
		pcgIter++;

		// NaN / divergence detection
		if (er != er || er > 1e10) {
			fprintf(stderr, "[fkn] CPU PCG diverged at iter %d: er=%g (n=%d)\n",
					pcgIter, er, n);
			return 0;
		}

		prg2=(int) (20.*log10(er)/(log10(Precision)));
		if(prg2>prg1){
			prg1=prg2;
			prg2=(prg1*5);
			if(prg2>100) prg2=100;
			TheView->m_prog1.SetPos(prg2);
			TheView->InvalidateRect(NULL, FALSE);
			TheView->UpdateWindow();
		}

	} while(er>Precision && pcgIter < maxPcgIter);

	if (er > Precision) {
		fprintf(stderr, "[fkn] CPU PCG stalled: er=%g after %d/%d iters (n=%d, prec=%g)\n",
				er, pcgIter, maxPcgIter, n, Precision);
		return 0;
	}

	return 1;
}

void CBigLinProb::SetValue(int i, double x)
{
    int k,fst,lst;
    double z;

	if(bdw==0){
		fst=0;
		lst=n;
	}
	else{
		fst=i-bdw; if (fst<0) fst=0;
		lst=i+bdw; if (lst>n) lst=n;
	}

    for(k=fst;k<lst;k++)
    {
        z=Get(k,i);
        if(z!=0){
            b[k]=b[k]-(z*x);
            if(i!=k) Put(0.,k,i);
        }
    }
    b[i]=Get(i,i)*x;
}

void CBigLinProb::Wipe()
{
	int i;

	// Reset pool — all off-diagonal entries are reclaimed in bulk.
	// Much faster than traversing every linked list to zero values.
	entryPool.reset();

	// Re-allocate diagonal entries from pool (fresh start)
	for(i=0;i<n;i++){
		M[i] = entryPool.alloc();
		M[i]->c = i;
		b[i]=0.;
	}

	// Invalidate CSR so it gets rebuilt before next solve
	csrReady = 0;
}

void CBigLinProb::AntiPeriodicity(int i, int j)
{
	int k,fst,lst;
	double v1,v2,c;

#ifdef KLUDGE
	int tmpbdw=bdw;
	bdw=0;
#endif

	if (j<i) {k=j;j=i;i=k;}

	if(bdw==0){
		fst=0;
		lst=n;
	}
	else{
		fst=i-bdw; if (fst<0) fst=0;
		lst=j+bdw; if (lst>n) lst=n;
	}

	for(k=fst;k<lst;k++)
	{
		if((k!=i) && (k!=j))
		{
			v1=Get(k,i);
			v2=Get(k,j);
			if ((v1!=0) || (v2!=0)){
				c=(v1-v2)/2.;
				Put(c,k,i);
				Put(-c,k,j);
			}
		}
		if((k==i+bdw) && (k<j-bdw) && (bdw!=0)) k=j-bdw;
	}

	c=0.5*(Get(i,i)+Get(j,j));
	Put(c,i,i);
	Put(c,j,j);

	c=0.5*(b[i]-b[j]);
	b[i]=c;
	b[j]=-c;

#ifdef KLUDGE
	bdw=tmpbdw;
#endif
}

void CBigLinProb::Periodicity(int i, int j)
{
	int k,fst,lst;
	double v1,v2,c;

#ifdef KLUDGE
	int tmpbdw=bdw;
	bdw=0;
#endif

	if (j<i) {k=j;j=i;i=k;}

	if(bdw==0){
		fst=0;
		lst=n;
	}
	else{
		fst=i-bdw; if (fst<0) fst=0;
		lst=j+bdw; if (lst>n) lst=n;
	}

	for(k=fst;k<lst;k++){
		if((k!=i) && (k!=j))
		{
			v1=Get(k,i);
			v2=Get(k,j);
			if ((v1!=0) || (v2!=0)) {
				c=(v1+v2)/2.;
				Put(c,k,i);
				Put(c,k,j);
			}
		}
		if((k==i+bdw) && (k<j-bdw) && (bdw!=0)) k=j-bdw;
	}

	c=(Get(i,i)+Get(j,j))/2.;
	Put(c,i,i);
	Put(c,j,j);

	c=0.5*(b[i]+b[j]);
	b[i]=c;
	b[j]=c;

#ifdef KLUDGE
	bdw=tmpbdw;
#endif
}


// a diagnostic routine to check whether that the bandwidth of the
// constructed matrix is actually consistent with a priori bandwidth.
void CBigLinProb::ComputeBandwidth()
{
	CEntry *e;
	int k,bw,maxbw;

	for(maxbw=0,k=0;k<n;k++)
	{
		e=M[k];
		while(e->next != NULL) e=e->next;
		bw=e->c - k;
		if (bw>maxbw) maxbw=bw;
	}

	MsgBox("Assumed Bandwidth = %i\nActual Bandwidth = %i",bdw,maxbw);
}
