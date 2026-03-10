// femmeDoc.h : interface of the CFemmeDoc class
//
/////////////////////////////////////////////////////////////////////////////
#define muo 1.2566370614359173e-6
#define Golden 0.3819660112501051517954131656

// ---------------------------------------------------------------
// Plain-C structs for passing GUI data to solver (no Qt dependency)
// ---------------------------------------------------------------
struct InMemMaterialProp {
	double mu_x, mu_y;
	double H_c, Theta_m;
	double Jr, Ji;
	double Cduct, Lam_d;
	double Theta_hn, Theta_hx, Theta_hy;
	double LamFill, WireD;
	int LamType, NStrands;
	int BHpoints;
	double *Bdata;     // caller-owned, solver copies
	double *Hdata_re;  // caller-owned (real part of H; imag=0)
};

struct InMemBoundaryProp {
	int BdryFormat;
	double A0, A1, A2, phi;
	double Mu, Sig;
	double c0_re, c0_im, c1_re, c1_im;
	double InnerAngle, OuterAngle;
};

struct InMemPointProp {
	double Jr, Ji;
	double Ar, Ai;
};

struct InMemCircuit {
	double dVolts_re, dVolts_im;
	double Amps_re, Amps_im;
	int CircType;
};

struct InMemBlockLabel {
	double x, y;
	double MaxArea, MagDir;
	int BlockType;   // index into material props
	int InCircuit;   // index into circuits (-1 = none)
	int InGroup, Turns;
	int IsExternal, IsDefault;
	const char *MagDirFctn;  // NULL or "" for none
};

struct InMemMeshNode {
	double x, y;
	int boundaryMarker;  // raw marker from Triangle output
};

struct InMemMeshElement {
	int p[3];
	int label;  // 1-based region attribute from Triangle
};

struct InMemMeshEdge {
	int n0, n1;
	int marker;  // raw marker from Triangle output
};

class CFemmeDocCore
{

// Attributes
public:

	CFemmeDocCore();
	~CFemmeDocCore();

	// General problem attributes
	double  Frequency;
	double  Precision;
	double  Relax;
	int		LengthUnits;
	int		ACSolver;
	BOOL    ProblemType;
	BOOL	Coords;
	CString	PrevSoln;
	int		PrevType;

	// Vector containing previous solution for incremental permeability analysis
	double *Aprev;

	// axisymmetric external region parameters
	double  extRo,extRi,extZo;

	CFknDlg *TheView;
	
	// CArrays containing the mesh information
	int	BandWidth;
	CNode *meshnode;
	CElement	*meshele;

	int NumNodes;
	int NumEls;
	
	// lists of properties
	int NumBlockProps;
	int NumPBCs;
	int NumLineProps;
	int NumPointProps;
	int NumCircProps;
	int NumBlockLabels;
	int NumCircPropsOrig;
	int NumAGEs;

	CMaterialProp	*blockproplist;
	CBoundaryProp	*lineproplist;
	CPointProp		*nodeproplist;
	CCircuit		*circproplist;
	CBlockLabel		*labellist;
	CCommonPoint	*pbclist;
	CAirGapElement  *agelist;
	// stuff usually kept track of by CDocument
	char *PathName;
	

// Operations
public:

	BOOL LoadPrev();
	BOOL LoadPrevA(CString myFile, double *V);
	BOOL LoadMesh();
	BOOL OnOpenDocument();

	// In-memory loading (replaces OnOpenDocument + LoadMesh for in-process use)
	BOOL LoadFromDocument(
		double frequency, double precision, double relax,
		int lengthUnits, int acSolver, int problemType, int coords,
		double extZo, double extRo, double extRi,
		const InMemMaterialProp *matProps, int numMatProps,
		const InMemBoundaryProp *bdryProps, int numBdryProps,
		const InMemPointProp *ptProps, int numPtProps,
		const InMemCircuit *circProps, int numCircProps,
		const InMemBlockLabel *blkLabels, int numBlkLabels);

	BOOL LoadMeshFromMemory(
		const InMemMeshNode *nodes, int numNodes,
		const InMemMeshElement *elements, int numElements,
		const InMemMeshEdge *edges, int numEdges);
	BOOL Cuthill();
	BOOL CuthillFromMemory(const InMemMeshEdge *edges, int numEdges);
	BOOL SortElements();
	BOOL Static2D(CBigLinProb &L);
	BOOL WriteStatic2D(CBigLinProb &L);
	BOOL Harmonic2D(CBigComplexLinProb &L);
	BOOL WriteHarmonic2D(CBigComplexLinProb &L);
	BOOL StaticAxisymmetric(CBigLinProb &L);
	BOOL HarmonicAxisymmetric(CBigComplexLinProb &L);
	void GetPrevAxiB(int k, double &B1p, double &B2p);
	void GetPrev2DB(int k, double &B1p, double &B2p);
	void GetFillFactor(int lbl);
	double ElmArea(int i);
	void CleanUp();

};

/////////////////////////////////////////////////////////////////////////////

double GetNewMu(double mu,int BHpoints, CComplex *BHdata,double muc,double B);
double Power(double x, int n);
