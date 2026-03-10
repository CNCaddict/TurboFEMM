#include <stdafx.h>
#include <math.h>
#include <stdio.h>
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"

#ifdef FEMM_USE_BLAS
  #ifdef __APPLE__
    #include <Accelerate/Accelerate.h>
  #else
    #include <cblas.h>
  #endif
#endif

#define MAXITER 1000000
#define KLUDGE
#define nrm(X) sqrt(Re(ConjDot(X,X)))

// CComplexCSR helper
void CComplexCSR::Free()
{
	free(D); D=NULL;
	free(R); R=NULL;
	free(C); C=NULL;
	free(V); V=NULL;
	nnz=0;
	ready=0;
}

CComplexEntry::CComplexEntry()
{
	next=NULL;
	x=0;
	c=0;
}

CBigComplexLinProb::CBigComplexLinProb()
{
	n=0;
}

CBigComplexLinProb::~CBigComplexLinProb()
{
	if (n==0) return;

	int i;
	CComplexEntry *uo,*ui;

	free(b); free(P); free(R);
	free(V); free(U); free(Z);
	free(uu); free(vv);

	for(i=0;i<n;i++)
	{
		ui=M[i];
		do{
			uo=ui;
			ui=uo->next;
			delete uo;
		} while(ui!=NULL);
	}
	free(M);

	if (bNewton)
	{
		for(i=0;i<n;i++)
		{
			ui=Mh[i];
			do{
				uo=ui;
				ui=uo->next;
				delete uo;
			} while(ui!=NULL);
		}
		free(Mh);

		for(i=0;i<n;i++)
		{
			ui=Ma[i];
			do{
				uo=ui;
				ui=uo->next;
				delete uo;
			} while(ui!=NULL);
		}
		free(Ma);

		for(i=0;i<n;i++)
		{
			ui=Ms[i];
			do{
				uo=ui;
				ui=uo->next;
				delete uo;
			} while(ui!=NULL);
		}
		free(Ms);
	}

	// Free CSR arrays
	csrM.Free();
	csrMh.Free();
	csrMa.Free();
	csrMs.Free();
}

int CBigComplexLinProb::Create(int d, int bw, int nodes)
{
	int i;

	bdw=bw;
	NumNodes=nodes;
	b=(CComplex *)calloc(d,sizeof(CComplex));
	V=(CComplex *)calloc(d,sizeof(CComplex));
	P=(CComplex *)calloc(d,sizeof(CComplex));
	R=(CComplex *)calloc(d,sizeof(CComplex));
	U=(CComplex *)calloc(d,sizeof(CComplex));
	Z=(CComplex *)calloc(d,sizeof(CComplex));
	uu=(CComplex *)calloc(d,sizeof(CComplex));
	vv=(CComplex *)calloc(d,sizeof(CComplex));
	n=d;

	M=(CComplexEntry **)calloc(d,sizeof(CComplexEntry *));
	for(i=0;i<d;i++){
		M[i] = new CComplexEntry;
		M[i]->c = i;
	}

	bNewton=FALSE;

	return 1;
}

void CBigComplexLinProb::Put(CComplex v, int p, int q, int k)
{
	CComplexEntry *e,*l;
	int i;

	if(q<p){
		i=p; p=q; q=i;
		if (k==1) v=conj(v);	// hermitian matrix
		if (k==3) v=-conj(v);	// antihermitian matrix
	}

	// allocate space for auxilliary matrices if they are actually needed
	if ((k>0) && (bNewton==FALSE))
	{
		bNewton=TRUE;

		Mh=(CComplexEntry **)calloc(n,sizeof(CComplexEntry *));
		for(i=0;i<n;i++){
			Mh[i] = new CComplexEntry;
			Mh[i]->c = i;
		}

		Ma=(CComplexEntry **)calloc(n,sizeof(CComplexEntry *));
		for(i=0;i<n;i++){
			Ma[i] = new CComplexEntry;
			Ma[i]->c = i;
		}

		Ms=(CComplexEntry **)calloc(n,sizeof(CComplexEntry *));
		for(i=0;i<n;i++){
			Ms[i] = new CComplexEntry;
			Ms[i]->c = i;
		}
	}

	switch(k)
	{
		case 1:
			e=Mh[p];
			break;
		case 2:
			e=Ms[p];
			break;
		case 3:
			e=Ma[p];
			break;
		default:
			e=M[p];
			break;
	}

	while((e->c < q) && (e->next != NULL))
	{
		l=e;
		e=e->next;
	}

	if(e->c == q){
		e->x=v;
		return;
	}

	CComplexEntry *m = new CComplexEntry;

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

CComplex CBigComplexLinProb::Get(int p, int q, int k)
{
	CComplexEntry *e;
	BOOL flip=FALSE;

	if(q<p){ int i; i=p; p=q; q=i; flip=TRUE; }

	switch(k)
	{
		case 1:
			if (bNewton==FALSE) return CComplex(0,0);
			e=Mh[p];
			break;
		case 2:
			if (bNewton==FALSE) return CComplex(0,0);
			e=Ms[p];
			break;
		case 3:
			if (bNewton==FALSE) return CComplex(0,0);
			e=Ma[p];
			break;
		default:
			e=M[p];
			break;
	}

	while((e->c < q) && (e->next != NULL)) e=e->next;

	if(e->c == q)
	{
		if(flip)
		{
			if(k==1) return conj(e->x);		// case where matrix is hermitian...
			if(k==3) return -conj(e->x);	// case where matrix is anti-hermitian...
		}

		return e->x;
	}

	// if no entry in the list, this entry must be zero...
	return CComplex(0,0);
}

void CBigComplexLinProb::AddTo(CComplex v, int p, int q)
{
	// Single-pass find-and-add on default matrix M (k=0)
	CComplexEntry *e, *l;
	int i;

	if(q < p){ i = p; p = q; q = i; }

	l = NULL;
	e = M[p];

	while((e->c < q) && (e->next != NULL)){
		l = e;
		e = e->next;
	}

	if(e->c == q){
		e->x += v;
		return;
	}

	// Entry doesn't exist yet — insert new one
	CComplexEntry *m = new CComplexEntry;
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

// Convert a single linked-list matrix to CSR format
void CBigComplexLinProb::FinalizeOne(CComplexEntry **LL, CComplexCSR &csr)
{
	int i;
	CComplexEntry *e;

	// Count off-diagonal entries
	csr.nnz = 0;
	for(i=0; i<n; i++){
		e = LL[i]->next;
		while(e != NULL){
			csr.nnz++;
			e = e->next;
		}
	}

	// Allocate
	csr.Free();
	csr.D = (CComplex *)malloc(n * sizeof(CComplex));
	csr.R = (int *)malloc((n+1) * sizeof(int));
	csr.C = (int *)malloc(csr.nnz * sizeof(int));
	csr.V = (CComplex *)malloc(csr.nnz * sizeof(CComplex));

	// Fill
	int pos = 0;
	for(i=0; i<n; i++){
		csr.D[i] = LL[i]->x;
		csr.R[i] = pos;
		e = LL[i]->next;
		while(e != NULL){
			csr.C[pos] = e->c;
			csr.V[pos] = e->x;
			pos++;
			e = e->next;
		}
	}
	csr.R[n] = pos;
	csr.ready = 1;
}

// Convert all linked-list matrices to CSR
void CBigComplexLinProb::Finalize()
{
	FinalizeOne(M, csrM);
	if(bNewton){
		FinalizeOne(Mh, csrMh);
		FinalizeOne(Ma, csrMa);
		FinalizeOne(Ms, csrMs);
	}
}

void CBigComplexLinProb::MultA(CComplex *X, CComplex *Y, int k)
{
	int i, j;
	CComplexEntry *e;

	for(i=0;i<n;i++) Y[i]=0;

	// force the program to give the plain matrix multiply
	// if auxilliary matrices have not been built
	if ((!bNewton) && (k!=0)) k=0;

	// Make the default call return the full multiply, including
	// the auxilliary matrix multiplies, when these matrices exist
	if ((bNewton) && (k==-1))
	{
		MultA(X,Y,0);
		MultA(X,uu,1);
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		MultConjA(X,uu,2);
		MultA(X,vv,3);
		for(i=0;i<n;i++) Y[i]=Y[i]+conj(uu[i])+vv[i];
		return;
	}
	if ((bNewton) && (k==-2))
	{
		MultA(X,Y,0);
		MultConjA(X,uu,1);
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		MultA(X,uu,2);
		MultConjA(X,vv,3);
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		for(i=0;i<n;i++) Y[i]=Y[i]+conj(uu[i])-vv[i];
		return;
	}

	// Select which CSR / linked-list to use
	CComplexCSR *csr = NULL;
	CComplexEntry **LL = NULL;
	switch(k){
		case 1: csr = &csrMh; LL = Mh; break;
		case 2: csr = &csrMs; LL = Ms; break;
		case 3: csr = &csrMa; LL = Ma; break;
		default: csr = &csrM; LL = M; break;
	}

	if(csr->ready){
		// CSR path
		for(i=0; i<n; i++){
			Y[i] += csr->D[i] * X[i];		// diagonal
			for(j=csr->R[i]; j<csr->R[i+1]; j++){
				int c = csr->C[j];
				CComplex v = csr->V[j];
				Y[i] += v * X[c];			// upper triangle
				if(k==1)
					Y[c] += conj(v) * X[i];	// hermitian
				else if(k==3)
					Y[c] += (-conj(v)) * X[i];	// antihermitian
				else
					Y[c] += v * X[i];			// complex-symmetric
			}
		}
	}
	else{
		// Linked-list fallback
		for(i=0;i<n;i++){
			Y[i] += LL[i]->x * X[i];
			e = LL[i]->next;
			while(e!=NULL){
				Y[i] += e->x * X[e->c];
				if(k==1)
					Y[e->c] += conj(e->x) * X[i];
				else if(k==3)
					Y[e->c] += (-conj(e->x)) * X[i];
				else
					Y[e->c] += e->x * X[i];
				e=e->next;
			}
		}
	}
}

void CBigComplexLinProb::MultConjA(CComplex *X, CComplex *Y, int k)
{
	int i, j;
	CComplexEntry *e;

	for(i=0;i<n;i++) Y[i]=0;

	if ((k!=0) && (!bNewton)) k=0;

	// Select which CSR / linked-list to use
	CComplexCSR *csr = NULL;
	CComplexEntry **LL = NULL;
	switch(k){
		case 1: csr = &csrMh; LL = Mh; break;
		case 2: csr = &csrMs; LL = Ms; break;
		case 3: csr = &csrMa; LL = Ma; break;
		default: csr = &csrM; LL = M; break;
	}

	if(csr->ready){
		// CSR path
		for(i=0; i<n; i++){
			Y[i] += csr->D[i].Conj() * X[i];
			for(j=csr->R[i]; j<csr->R[i+1]; j++){
				int c = csr->C[j];
				CComplex v = csr->V[j];
				Y[i] += v.Conj() * X[c];
				if(k==1)
					Y[c] += v * X[i];			// hermitian
				else if(k==3)
					Y[c] += (-v) * X[i];		// antihermitian
				else
					Y[c] += v.Conj() * X[i];	// complex-symmetric
			}
		}
	}
	else{
		// Linked-list fallback
		for(i=0;i<n;i++){
			Y[i] += LL[i]->x.Conj() * X[i];
			e = LL[i]->next;
			while(e!=NULL){
				Y[i] += e->x.Conj() * X[e->c];
				if(k==1)
					Y[e->c] += e->x * X[i];
				else if(k==3)
					Y[e->c] += (-e->x) * X[i];
				else
					Y[e->c] += e->x.Conj() * X[i];
				e=e->next;
			}
		}
	}
}

void CBigComplexLinProb::MultAPPA(CComplex *X, CComplex *Y)
{
	int i;
	MultA(X,Z);
	MultPC(Z,Y);
	for(i=0;i<n;i++) Y[i].im=-Y[i].im;
	MultPC(Y,Z);
	MultA(Z,Y);
	for(i=0;i<n;i++) Y[i].im=-Y[i].im;
}

CComplex CBigComplexLinProb::Dot(CComplex *x, CComplex *y)
{
#ifdef FEMM_USE_BLAS
	// CComplex has {double re, im} — same memory layout as BLAS complex
	CComplex z;
	cblas_zdotu_sub(n, (const double*)x, 1, (const double*)y, 1, (double*)&z);
	return z;
#else
	int i;
	CComplex z;

	z=0;
	for(i=0;i<n;i++) z+=x[i]*y[i];

	return z;
#endif
}

CComplex CBigComplexLinProb::ConjDot(CComplex *x, CComplex *y)
{
#ifdef FEMM_USE_BLAS
	CComplex z;
	cblas_zdotc_sub(n, (const double*)x, 1, (const double*)y, 1, (double*)&z);
	return z;
#else
	int i;
	CComplex z;

	z=0;
	for(i=0;i<n;i++) z+=x[i].Conj()*y[i];

	return z;
#endif
}

// Preconditioner for complex matrices: Y = M^{-1} * X
// Default is Jacobi (diagonal scaling) — trivially parallel, GPU-friendly.
// SSOR is available as a fallback via FEMM_USE_SSOR_PRECOND.
void CBigComplexLinProb::MultPC(CComplex *X, CComplex *Y)
{
	int i, j;

#ifndef FEMM_USE_SSOR_PRECOND
	// Jacobi preconditioner: Y[i] = X[i] / diag[i]
	if(csrM.ready){
		for(i=0; i<n; i++) Y[i] = X[i] / csrM.D[i];
	}
	else{
		for(i=0; i<n; i++) Y[i] = X[i] / M[i]->x;
	}
#else
	// SSOR preconditioner (sequential, CPU only)
	if(csrM.ready){
		CComplex s = LAMBDA*(2.-LAMBDA);
		for(i=0;i<n;i++) Y[i]=X[i]*s;

		for(i=0;i<n;i++){
			Y[i] /= csrM.D[i];
			for(j=csrM.R[i]; j<csrM.R[i+1]; j++){
				Y[csrM.C[j]] -= csrM.V[j] * Y[i] * LAMBDA;
			}
		}
		for(i=0;i<n;i++) Y[i] *= csrM.D[i];
		for(i=n-1;i>=0;i--){
			for(j=csrM.R[i]; j<csrM.R[i+1]; j++){
				Y[i] -= csrM.V[j] * Y[csrM.C[j]] * LAMBDA;
			}
			Y[i] /= csrM.D[i];
		}
	}
	else{
		CComplexEntry *e;
		CComplex c = LAMBDA*(2.-LAMBDA);
		for(i=0;i<n;i++) Y[i]=X[i]*c;

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

void CBigComplexLinProb::SetValue(int i, CComplex x)
{
    int k,fst,lst;
    CComplex z;

	if(bdw==0){
		fst=0;
		lst=n;
	}
	else{
		fst=i-bdw; if (fst<0) fst=0;
		lst=i+bdw; if (lst>NumNodes) lst=NumNodes;
	}

	for(k=fst;k<n;k++)
    {
		if (k==lst) k=NumNodes;

		z=Get(k,i);
		if(z!=0){
			b[k]-=(z*x);
			if(i!=k) Put(CComplex(0,0),k,i);
		}

		if (bNewton)
		{
			z=Get(k,i,1);
			if(z!=0){
				if (i!=k) b[k]=b[k]-(z*x);
				Put(CComplex(0,0),k,i,1);
			}

			z=Get(k,i,2);
			if(z!=0){
				if (i!=k) b[k]=b[k]-(z*conj(x));
				Put(CComplex(0,0),k,i,2);
			}

			z=Get(k,i,3);
			if(z!=0){
				if (i!=k) b[k]=b[k]-(z*conj(x));
				Put(CComplex(0,0),k,i,3);
			}
		}
    }
    b[i]=Get(i,i)*x;
}

void CBigComplexLinProb::Wipe()
{
	int i;
	CComplexEntry *e;

	for(i=0;i<n;i++){
		b[i]=0;
		e=M[i];
		do{
			e->x=0;
			e=e->next;
		} while(e!=NULL);
	}

	if (!bNewton) return;

	for(i=0;i<n;i++){
		e=Mh[i];
		do{
			e->x=0;
			e=e->next;
		} while(e!=NULL);
	}

	for(i=0;i<n;i++){
		e=Ma[i];
		do{
			e->x=0;
			e=e->next;
		} while(e!=NULL);
	}

	for(i=0;i<n;i++){
		e=Ms[i];
		do{
			e->x=0;
			e=e->next;
		} while(e!=NULL);
	}

	// Invalidate CSR
	csrM.ready = 0;
	csrMh.ready = 0;
	csrMa.ready = 0;
	csrMs.ready = 0;
}

void CBigComplexLinProb::AntiPeriodicity(int i, int j)
{
	int k,fst,lst,h;
	CComplex v1,v2,c;

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
		lst=j+bdw; if (lst>NumNodes-1) lst=NumNodes-1;
	}

	// contribution to A0 matrix
	for(k=fst;k<n;k++)
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
		else if(k==lst) k=NumNodes;
	}
	c=0.5*(Get(i,i)+Get(j,j));
	Put(c,i,i);
	Put(c,j,j);

	// contribution to RHS
	c=0.5*(b[i]-b[j]);
	b[i]=c;
	b[j]=-c;

    if(bNewton) for(h=1;h<=3;h++)
	{
		for(k=fst;k<n;k++)
		{
			if((k!=i) && (k!=j))
			{
				v1=Get(k,i,h);
				v2=Get(k,j,h);
				if ((v1!=0) || (v2!=0)){
					c=(v1-v2)/2.;
					Put(c,k,i,h);
					Put(-c,k,j,h);
				}
			}
			if((k==i+bdw) && (k<j-bdw) && (bdw!=0)) k=j-bdw;
			else if(k==lst) k=NumNodes;
		}
		c=(Get(i,i,h)-Get(i,j,h)-Get(j,i,h)+Get(j,j,h))/4.;
		Put(c,i,i,h);
		Put(-c,i,j,h);
		Put(c,j,j,h);
	}

#ifdef KLUDGE
	bdw=tmpbdw;
#endif
}

void CBigComplexLinProb::Periodicity(int i, int j)
{
	int k,fst,lst,h;
	CComplex v1,v2,c;

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
		lst=j+bdw; if (lst>NumNodes-1) lst=NumNodes-1;
	}

	for(k=fst;k<n;k++){
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
		else if(k==lst) k=NumNodes;
	}

	c=(Get(i,i)+Get(j,j))/2.;
	Put(c,i,i);
	Put(c,j,j);

	c=0.5*(b[i]+b[j]);
	b[i]=c;
	b[j]=c;

    if(bNewton) for(h=1;h<=3;h++)
	{
		for(k=fst;k<n;k++)
		{
			if((k!=i) && (k!=j))
			{
				v1=Get(k,i,h);
				v2=Get(k,j,h);
				if ((v1!=0) || (v2!=0)){
					c=(v1+v2)/2.;
					Put(c,k,i,h);
					Put(c,k,j,h);
				}
			}
			if((k==i+bdw) && (k<j-bdw) && (bdw!=0)) k=j-bdw;
			else if(k==lst) k=NumNodes;
		}
		c=(Get(i,i,h)+Get(i,j,h)+Get(j,i,h)+Get(j,j,h))/4.;
		Put(c,i,i,h);
		Put(c,i,j,h);
		Put(c,j,j,h);
	}
}

// Make into a Hermitian problem and solve.
// Just use for a few iterations to get a good starting point
// for the regular BiPCG, which can sometimes get initialized
// with a pathological starting point.
int CBigComplexLinProb::PCGSQStart()
{
	int i,k;
	CComplex res,res_new,del,rho,pAp;

	// quick check for most obvious sign of singularity;
	for(i=0;i<n;i++) if((M[i]->x.re==0) && (M[i]->x.im==0)){
		fprintf(stderr,"singular flag tripped.");
		return 0;
	}

	// Operate on RHS to scale for squared problem
	MultPC(b,Z);
	for(i=0;i<n;i++) Z[i].im=-Z[i].im;
	MultPC(Z,P);
	MultA(P,Z);
	for(i=0;i<n;i++) P[i]=Z[i].Conj();

	// initialize V with zeros;
	for(i=0;i<n;i++) V[i]=0;

	// form residual;
	MultAPPA(V,R);
	for(i=0;i<n;i++) R[i]=P[i]-R[i];

	// form initial search direction
	for(i=0;i<n;i++) P[i]=R[i];
	res=ConjDot(R,R);

	// do iteration;
	for(k=0;k<3;k++)
	{
		// step i)
		MultAPPA(P,U);
		pAp=ConjDot(P,U);
		del=res/pAp;

		// step ii)
		for(i=0;i<n;i++) V[i]+=(del*P[i]);

		// step iii)
		for(i=0;i<n;i++) R[i]-=(del*U[i]);

		// step iv)
		res_new=ConjDot(R,R);
		rho=res_new/res;
		res=res_new;

		// step v)
		for(i=0;i<n;i++) P[i]=R[i]+(rho*P[i]);

	}

	return 1;
}

// Complex-Symmetric Preconditioned BiCG
int CBigComplexLinProb::PBCGSolve(int flag)
{
	int i;
	CComplex res,res_new,del,rho,pAp;
	double er,normb;
	int prg2,prg1=0;

	// Initialize if required
	if(flag==FALSE){
		for(i=0;i<n;i++) V[i]=0;
	}

	// form residual;
	MultA(V,R);
	for(i=0;i<n;i++) R[i]=b[i]-R[i];
	normb=nrm(b);

	// initialize progress bar;
	er=nrm(R)/normb;
	prg1=(int) (20.*log10(er)/(log10(Precision)));
	TheView->m_prog1.SetPos(5*prg1);
	TheView->SetDlgItemText(IDC_FRAME1,"BiConjugate Gradient Solver");
	TheView->InvalidateRect(NULL, FALSE);
	TheView->UpdateWindow();

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

		// step ii)
		for(i=0;i<n;i++) V[i]+=(del*P[i]);

		// step iii)
		for(i=0;i<n;i++) R[i]-=(del*U[i]);

		// step iv)
		MultPC(R,Z);
		res_new=Dot(Z,R);
		rho=res_new/res;
		res=res_new;

		// step v)
		for(i=0;i<n;i++) P[i]=Z[i]+(rho*P[i]);

		er=nrm(R)/normb;
		pcgIter++;

		// NaN / divergence detection
		if (er != er || er > 1e10) {
			fprintf(stderr, "[fkn] PBCG diverged at iter %d: er=%g (n=%d)\n",
					pcgIter, er, n);
			return 0;
		}

		// report progress
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
		fprintf(stderr, "[fkn] PBCG stalled: er=%g after %d/%d iters (n=%d, prec=%g)\n",
				er, pcgIter, maxPcgIter, n, Precision);
		return 0;
	}

	return 1;
}

// BiCGSTAB for solving N-R iterations
int CBigComplexLinProb::BiCGSTAB(int flag)
{
	double er,normb;
	CComplex om,alf,rho1,rho2,bta;
	CComplex *P2,*R2,*Z2,*t;
	int i,j,k;
	CString out;

	P2=(CComplex *)calloc(n,sizeof(CComplex));
	Z2=(CComplex *)calloc(n,sizeof(CComplex));
	R2=(CComplex *)calloc(n,sizeof(CComplex));
	t =(CComplex *)calloc(n,sizeof(CComplex));

	// initialize progress bar;
	TheView->m_prog1.SetPos(0);
	int prg1=0;
	int prg2;
	TheView->SetDlgItemText(IDC_FRAME1,"BiCGSTAB Solver");

	if (flag==FALSE) for(i=0;i<n;i++) V[i]=0;

	MultA(V,R,-1);
	for(j=0;j<n;j++){
		R[j]=b[j]-R[j];
		R2[j]=R[j];
	}

	normb=nrm(b);

	for(k=0;k<MAXITER;k++)
	{
		rho1 = Re(ConjDot(R2,R));
		if (k==0){
			for(j=0;j<n;j++) P[j]=R[j];
		}
		else{
			bta=(rho1/rho2)*alf/om;
			for(j=0;j<n;j++) P[j] = R[j] + bta*(P[j] - om*U[j]);
		}
		MultPC(P,P2);
		MultA(P2,U,-1);
		alf=rho1/Re(ConjDot(R2,U));
		for(j=0;j<n;j++) Z[j]=R[j]-alf*U[j];
		MultPC(Z,Z2);
		MultA(Z2,t,-1);
		om=Re(ConjDot(t,Z))/Re(ConjDot(t,t));
		for(j=0;j<n;j++){
			V[j]=V[j]+alf*P2[j]+om*Z2[j];
			R[j]=Z[j]-om*t[j];
		}
		rho2 = rho1;
		er=nrm(R)/normb;

		// display progress to the user
		if (k==50*(k/50)){
			out.Format("BiCGSTAB Solver (%i,%g)",k,er);
			TheView->SetDlgItemText(IDC_FRAME1,out);
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

		if (er<Precision) break;
	}
	free(P2);
	free(R2);
	free(Z2);
	free(t);

	if (er<Precision) return 1;
	return 0;
}

// ad-hoc iterative approach to solving non-symmetric N-R problem
int CBigComplexLinProb::KludgeSolve(int flag)
{

	int i,k;
	double er,normb,c;
	CString out;
	CComplex *borig, *v, *r;

	borig=(CComplex *)calloc(n,sizeof(CComplex));
	v    =(CComplex *)calloc(n,sizeof(CComplex));
	r    =(CComplex *)calloc(n,sizeof(CComplex));

	// if flag is false, initialize V with zeros;
	if (flag==0) for(i=0;i<n;i++) V[i]=0;

	// get norm of RHS
	normb=nrm(b);

	// save original b vector
	for (i=0;i<n;i++){
		borig[i]=b[i];
		v[i]=V[i];
	}

	// form starting residual
	MultA(V,r,-1);
	for(i=0;i<n;i++) r[i]=b[i]-r[i];
	er=nrm(r)/normb;
	if (er<Precision) return 1;

	for(k=0;k<10;k++)
	{
		// modify RHS multiplying results of the previous
		// iteration by the A1 and A2 matrices
		MultA(V,P,1);
		MultConjA(V,U,2);
		MultA(V,R,3);
		for(i=0;i<n;i++) b[i]=borig[i] - P[i] - conj(U[i]) - R[i];

		PBCGSolve(TRUE);

		// adjust step length along the new direction
		// to result in the greatest reduction in error
		for(i=0;i<n;i++) P[i]= V[i]-v[i];
		MultA(P,U,-1);
		c=Re(ConjDot(r,U))/Re(ConjDot(U,U));
		for(i=0;i<n;i++)
		{
			V[i] = v[i] + c*P[i];
			r[i] = r[i] - c*U[i];
			v[i] = V[i];
		}

		er=nrm(r)/normb;
		if (er<Precision*10.) break;
	}
	free(borig); free(v); free(r);
	return 1;
}

// Entry point into linear solvers.
// Calls PCGSQStart to do a small number of iterations,
// moving the starting point for PBCG away from the
// pathological starting points that can sometimes crop up.
int CBigComplexLinProb::PBCGSolveMod(int flag)
{
	// Convert linked-list to CSR before iterating
	Finalize();

	// if this is a N-R iteration, call the appropriate solver
	if (bNewton)
	//	return BiCGSTAB(flag);
		return KludgeSolve(flag);

	// Get starting point with a few iterations of CGNE;
	if(flag==FALSE){
		TheView->SetDlgItemText(IDC_FRAME1,"Initializing Solver");
		if (PCGSQStart()==0) return 0;
	}


	// call the complex-symmetric solver
	return PBCGSolve(2);
}
