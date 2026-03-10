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

class CBigComplexLinProb
{
	public:

		// data members
		BOOL *Q;
		CComplex *P;
		CComplex *U;
	 	CComplex *R;
		CComplex *V;
		CComplex *Z;
		CComplex *b;				// RHS of linear equation
		CComplexEntry **M;			// pointer to list of matrix entries;
		int n;						// dimensions of the matrix;
		int bdw;					// optional bandwidth parameter;
		int NumNodes;
		double Precision;
		CComplex Lambda;

		// CSR storage (populated by Finalize(), used by MultA/MultPC)
		CComplex *csrD;				// diagonal values [n]
		int      *csrR;				// row pointers [n+1]
		int      *csrC;				// column indices [nnz_offdiag]
		CComplex *csrV;				// off-diagonal values [nnz_offdiag]
		int       csrNnz;			// number of off-diagonal entries
		int       csrReady;			// 1 after Finalize() has been called

		// member functions

		CBigComplexLinProb();				// constructor
		~CBigComplexLinProb();				// destructor
		int Create(int d, int bw, int nodes);	// initialize the problem
		void Put(CComplex v, int p, int q);
								// use to create/set entries in the matrix
		CComplex Get(int p, int q);
		void Finalize();			// convert linked-list to CSR
		int PBCGSolve(int flag);	// flag==true if guess for V present;
		int QMRSolve(int flag);
		void MultA(CComplex *X, CComplex *Y);
		void MultConjA(CComplex *X, CComplex *Y);
		CComplex Dot(CComplex *x, CComplex *y);
		CComplex ConjDot(CComplex *x, CComplex *y);
		void SetValue(int i, CComplex x);
		void Periodicity(int i, int j);
		void AntiPeriodicity(int i, int j);
		void Wipe();
		void MultPC(CComplex *X, CComplex *Y);
		void MultAPPA(CComplex *X, CComplex *Y);
		int PCGSQSolve(int flag);
		int PCGSQStart(int flag);
		int PBCGSolveMod(int flag);

		CcsolvDlg *TheView;

	private:

};
