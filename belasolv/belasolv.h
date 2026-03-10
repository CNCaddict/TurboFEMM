// belasolv.h : main header file for the BELASOLV application
//

#ifdef _WIN32
#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif
#endif

#include "resource.h"		// main symbols

void old_main(void *inptr);

#ifdef _WIN32
/////////////////////////////////////////////////////////////////////////////
// CbelasolvApp:
// See belasolv.cpp for the implementation of this class
//

class CbelasolvApp : public CWinApp
{
public:
	CbelasolvApp();
	~CbelasolvApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CbelasolvApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CbelasolvApp)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

#endif // _WIN32

/////////////////////////////////////////////////////////////////////////////
