class CEntry
{
	public:

		double x;				// value stored in the entry
		int c;					// column that the entry lives in
		CEntry *next;			// pointer to next entry in row;
		CEntry();
		
	private:
};

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
		BOOL   *Q;
		CEntry **M;				// pointer to list of matrix entries;
		int n;					// dimensions of the matrix;
		int bdw;				// Optional matrix bandwidth parameter;
		double Lambda;			// relaxation factor;
		double Precision;		// error tolerance for solution

		// CSR storage (populated by Finalize(), used by MultA/MultPC)
		double *csrD;			// diagonal values [n]
		int    *csrR;			// row pointers [n+1]
		int    *csrC;			// column indices [nnz_offdiag]
		double *csrV;			// off-diagonal values [nnz_offdiag]
		int     csrNnz;			// number of off-diagonal entries
		int     csrReady;		// 1 after Finalize() has been called

		// member functions

		CBigLinProb();				// constructor
		~CBigLinProb();				// destructor
		int Create(int d, int bw);			// initialize the problem
		void Put(double v, int p, int q);
									// use to create/set entries in the matrix
		double Get(int p, int q);
		void Finalize();			// convert linked-list to CSR
		int PCGSolve(int flag);		 // flag==true if guess for V present;
		void MultPC(double *X, double *Y);
		void MultA(double *X, double *Y);
		void SetValue(int i, double x);
		void Periodicity(int i, int j);
		void AntiPeriodicity(int i, int j);
		void Wipe();
		double Dot(double *X, double *Y);
		
		CbelasolvDlg *TheView;

	private:

};

