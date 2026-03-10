#include<stdafx.h>
#include<stdio.h>
#include<math.h>
#include<string.h>
#ifdef _WIN32
#include<afxtempl.h>
#endif
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"
#include "mesh.h"
#include "FemmeDocCore.h"

#ifndef _WIN32
#include <sys/time.h>
static double wallclock_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

void old_main(void *inptr)
{
	CFknDlg *TheView;
	CFemmeDocCore Doc;
	char PathName[256];
	CFileDialog *fname_dia;
	char outstr[1024];
	int i;

	TheView=(CFknDlg *) inptr;

	if (__argc<2){

		fname_dia=new CFileDialog(
			TRUE,
			"fem | * ",
			NULL,
			OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
			"FEMM datafile (*.fem) | *.fem; *.FEM | All Files (*.*) | *.*||",
			NULL);

		if(fname_dia->DoModal()==IDCANCEL){
			delete fname_dia;
			MsgBox("No file name!");
			return;
		}

		CString fname=fname_dia->GetPathName();
		fname=fname.Left(fname.GetLength()-4);
		strcpy(PathName,fname);
		delete fname_dia;
	}
	else strcpy(PathName,__argv[1]);

	Doc.PathName=PathName;
	Doc.TheView=TheView;

#ifndef _WIN32
	double t0 = wallclock_ms(), t1;
#endif

	if (Doc.OnOpenDocument()!=TRUE){
		MsgBox("problem loading .fem file");
		exit(7);
	}

#ifndef _WIN32
	t1 = wallclock_ms();
	fprintf(stderr, "[fkn] OnOpenDocument: %.1f ms\n", t1-t0); t0=t1;
#endif

	// load mesh
	if (Doc.LoadMesh()!=TRUE){
		if (Doc.PrevSoln.GetLength()>0) MsgBox("problem loading mesh from previous solution");
		else MsgBox("problem loading mesh");
		exit(2);
	}

#ifndef _WIN32
	t1 = wallclock_ms();
	fprintf(stderr, "[fkn] LoadMesh: %.1f ms\n", t1-t0); t0=t1;
#endif

	// label the dialog to report which problem is being solved
	char PaneText[256];
	char *ProbName;
	ProbName=PathName;
	for(i=0;i< (int) strlen(PathName);i++)
		if(PathName[i]=='\\') ProbName=PathName+i;
	if (strlen(PathName)>0){
		ProbName++;
		sprintf(PaneText,"%s - fkern",ProbName);
	}

	while(!IsWindow(TheView->m_hWnd)) Sleep(1);
	TheView->SetWindowText(PaneText);

	// renumber using Cuthill-McKee
	// (renumbering not needed if using previous solution mesh)
	if (Doc.PrevType==0)
	{
		TheView->SetDlgItemText(IDC_STATUSWINDOW,"renumbering nodes");
		if (Doc.Cuthill()!=TRUE){
			MsgBox("problem renumbering node points");
			exit(3);
		}
	}

#ifndef _WIN32
	t1 = wallclock_ms();
	fprintf(stderr, "[fkn] Cuthill: %.1f ms\n", t1-t0); t0=t1;
#endif

	TheView->SetDlgItemText(IDC_STATUSWINDOW,"solving...");
	sprintf(outstr,"Problem Statistics:\n%i nodes\n%i elements\nPrecision: %3.2e\n",
			Doc.NumNodes,Doc.NumEls,Doc.Precision);
	TheView->SetDlgItemText(IDC_PROBSTATS,outstr);

	double mr=(8.*((double) Doc.NumNodes)*((double) Doc.BandWidth))/ 1.e06;

	if(Doc.Frequency==0){
		CBigLinProb L;
		L.TheView=TheView;
		L.Precision=Doc.Precision;
		// initialize the problem, allocating the space required to solve it.
		if (L.Create(Doc.NumNodes,Doc.BandWidth)==FALSE){
			MsgBox("couldn't allocate enough space for matrices");
			exit(4);
		}

#ifndef _WIN32
		t1 = wallclock_ms();
		fprintf(stderr, "[fkn] L.Create: %.1f ms\n", t1-t0); t0=t1;
#endif

		// Create element matrices and solve the problem;
		if (Doc.ProblemType==FALSE){
			if (Doc.Static2D(L)==FALSE){
				MsgBox("Couldn't solve the problem");
				exit(5);
			}
			TheView->SetDlgItemText(IDC_STATUSWINDOW,"Static 2-D problem solved");
		}
		else{
			if (Doc.StaticAxisymmetric(L)==FALSE){
				MsgBox("Couldn't solve the problem");
				exit(5);
			}
			TheView->SetDlgItemText(IDC_STATUSWINDOW,"Static axisymmetric problem solved");
		}

#ifndef _WIN32
		t1 = wallclock_ms();
		fprintf(stderr, "[fkn] Assemble+Solve: %.1f ms\n", t1-t0); t0=t1;
#endif

		if (Doc.WriteStatic2D(L)==FALSE)
		{
			MsgBox("couldn't write results to disk");
			exit(6);
		}
		TheView->SetDlgItemText(IDC_STATUSWINDOW,"results written to disk");

#ifndef _WIN32
		t1 = wallclock_ms();
		fprintf(stderr, "[fkn] WriteStatic2D: %.1f ms\n", t1-t0); t0=t1;
#endif
	}

	else{
		CBigComplexLinProb L;
		L.TheView=TheView;
		L.Precision=Doc.Precision;

		// initialize the problem, allocating the space required to solve it.
		if (L.Create(Doc.NumNodes+Doc.NumCircProps,Doc.BandWidth,Doc.NumNodes)==FALSE){
				MsgBox("couldn't allocate enough space for matrices");
				exit(4);
		}

		// Create element matrices and solve the problem;
		if (Doc.ProblemType==FALSE){
			if (Doc.Harmonic2D(L)==FALSE){
				MsgBox("Couldn't solve the problem");
				exit(5);
			}
			TheView->SetDlgItemText(IDC_STATUSWINDOW,"Harmonic 2-D problem solved");
		}
		else{
		if (Doc.HarmonicAxisymmetric(L)==FALSE){
				MsgBox("Couldn't solve the problem");
				exit(5);
			}
			TheView->SetDlgItemText(IDC_STATUSWINDOW,"Harmonic axisymmetric problem solved");
		}


		if (Doc.WriteHarmonic2D(L)==FALSE)
		{
			MsgBox("couldn't write results to disk");
			exit(6);
		}
		TheView->SetDlgItemText(IDC_STATUSWINDOW,"results written to disk.");
	}

	Doc.CleanUp();

	exit(0);
}

#ifndef _WIN32
// Portable entry point for macOS/Linux
int __argc_compat;
char** __argv_compat;

#include "lua.h"
extern void lua_baselibopen (lua_State *L);
extern void lua_iolibopen (lua_State *L);
extern void lua_strlibopen (lua_State *L);
extern void lua_mathlibopen (lua_State *L);

lua_State *lua; // the main lua object

int main(int argc, char *argv[])
{
	__argc_compat = argc;
	__argv_compat = argv;

	// Initialize Lua
	lua = lua_open(4096);
	lua_baselibopen(lua);
	lua_strlibopen(lua);
	lua_mathlibopen(lua);
	lua_iolibopen(lua);

	CFknDlg dlg;
	old_main(&dlg);

	lua_close(lua);
	return 0;
}
#endif
