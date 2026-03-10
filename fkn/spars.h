class CEntry
{
	public:

		double x;				// value stored in the entry
		int c;					// column that the entry lives in
		CEntry *next;			// pointer to next entry in row;
		CEntry();

	private:
};

// Pool allocator for CEntry nodes — eliminates millions of individual
// malloc/free calls during matrix assembly.  All nodes live in contiguous
// blocks, improving cache locality for linked-list traversal.
class CEntryPool
{
	public:
		CEntryPool();
		~CEntryPool();
		CEntry *alloc();		// grab next entry from pool
		void reset();			// reclaim all entries (Wipe)

	private:
		static const int BLOCK_SIZE = 65536;
		struct Block {
			CEntry entries[BLOCK_SIZE];
		};
		Block **blocks;			// array of block pointers
		int numBlocks;			// number of blocks currently in use
		int allocBlocks;		// number of blocks actually allocated (>= numBlocks)
		int maxBlocks;			// capacity of blocks pointer array
		int nextInBlock;		// next entry in current block

		void addBlock();
};

#define LAMBDA 1.5

class CBigLinProb
{
	public:

		// data members

		double *V;				// solution
		double *P;				// search direction;
		double *R;				// residual;
		double *U;				// A * P;
		double *Z;
		double *b;				// RHS of linear equation
		CEntry **M;				// pointer to list of matrix entries;
		int n;					// dimensions of the matrix;
		int bdw;				// Optional matrix bandwidth parameter;
		double Precision;		// error tolerance for solution

		// CSR storage (populated by Finalize(), used by MultA/MultPC)
		double *csrD;			// diagonal values [n]
		int    *csrR;			// row pointers [n+1]
		int    *csrC;			// column indices [nnz_offdiag]
		double *csrV;			// off-diagonal values [nnz_offdiag]
		int     csrNnz;			// number of off-diagonal entries
		int     csrReady;		// 1 after Finalize() has been called

		// Pool allocator for off-diagonal CEntry nodes
		CEntryPool entryPool;

		// member functions

		CBigLinProb();				// constructor
		~CBigLinProb();				// destructor
		int Create(int d, int bw);			// initialize the problem
		void Put(double v, int p, int q);
									// use to create/set entries in the matrix
		double Get(int p, int q);
		void AddTo(double v, int p, int q);
		void Finalize();			// convert linked-list to CSR
		int PCGSolve(int flag);		 // flag==true if guess for V present;
		void MultPC(double *X, double *Y);
		void MultA(double *X, double *Y);
		void SetValue(int i, double x);
		void Periodicity(int i, int j);
		void AntiPeriodicity(int i, int j);
		void Wipe();
		double Dot(double *X, double *Y);
		void ComputeBandwidth();

		CFknDlg *TheView;

#ifdef FEMM_USE_METAL
		// Optional persistent GPU backend (owned externally).
		// If set, PCGSolve uses this instead of creating a new one per call.
		// Saves ~15ms of Metal device/pipeline init per solve.
		class GPUBackend *externalGPU;
#endif

	private:

};


/////////////////////////////////////////////////////////////////////
// for complex matrices......

class CComplexEntry
{
	public:

		CComplex x;				// value stored in the entry
		int c;					// column that the entry lives in
		CComplexEntry *next;	// pointer to next entry in the row;
		CComplexEntry();
		
	private:
};

// CSR storage for a single complex sparse matrix (upper triangle + diagonal)
struct CComplexCSR
{
	CComplex *D;		// diagonal values [n]
	int      *R;		// row pointers [n+1]
	int      *C;		// column indices [nnz_offdiag]
	CComplex *V;		// off-diagonal values [nnz_offdiag]
	int       nnz;		// number of off-diagonal entries
	int       ready;	// 1 after populated

	CComplexCSR() : D(NULL), R(NULL), C(NULL), V(NULL), nnz(0), ready(0) {}
	void Free();
};

class CBigComplexLinProb
{
	public:

		// data members

		CComplex *P; 				// search direction
		CComplex *U;
		CComplex *R; 				// residual
		CComplex *V;
		CComplex *Z;
		CComplex *b;				// RHS of linear equation
		CComplex *uu;
		CComplex *vv;

		CComplexEntry **M;			// pointer to list of matrix entries;
		CComplexEntry **Mh;			// Hermitian matrix arising from N-R algorithm;
		CComplexEntry **Ma;			// Antihermitian matrix arising from N-R algorithm;
		CComplexEntry **Ms;			// Additional complex-symmetric matrix arising from N-R algorithm;
		int n;						// dimensions of the matrix;
		int bdw;					// optional bandwidth parameter;
		int bNewton;				// Flag which denotes whether or not there are entries in Mh or Ms;
		int NumNodes;
		double Precision;

		// CSR storage for each matrix
		CComplexCSR csrM;			// main matrix
		CComplexCSR csrMh;			// hermitian matrix (N-R)
		CComplexCSR csrMa;			// antihermitian matrix (N-R)
		CComplexCSR csrMs;			// complex-symmetric matrix (N-R)

		// member functions

		CBigComplexLinProb();				// constructor
		~CBigComplexLinProb();				// destructor
		int Create(int d, int bw, int nodes);	// initialize the problem
		void Put(CComplex v, int p, int q, int k=0); // use to create/set entries in the matrix
		CComplex Get(int p, int q, int k=0);
		void AddTo(CComplex v, int p, int q);
		void Finalize();			// convert all linked-lists to CSR
		void FinalizeOne(CComplexEntry **LL, CComplexCSR &csr);
		void MultA(CComplex *X, CComplex *Y, int k=0);
		void MultConjA(CComplex *X, CComplex *Y, int k=0);
		CComplex Dot(CComplex *x, CComplex *y);
		CComplex ConjDot(CComplex *x, CComplex *y);
		void SetValue(int i, CComplex x);
		void Periodicity(int i, int j);
		void AntiPeriodicity(int i, int j);
		void Wipe();
		void MultPC(CComplex *X, CComplex *Y);
		void MultAPPA(CComplex *X, CComplex *Y);


		// flag==false initializes solution to zero
		// flag==true  starts from solution of previous call
		int PBCGSolveMod(int flag);	// Precondition Biconjugate Gradient
		int PCGSQStart();
		int PBCGSolve(int flag);
		int BiCGSTAB(int flag);
		int KludgeSolve(int flag);

		CFknDlg *TheView;

	private:

};

