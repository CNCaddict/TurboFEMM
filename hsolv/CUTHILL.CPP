// Cuthill-McKee algorithm — hsolv solver wrapper
#include<stdafx.h>
#ifdef _WIN32
#include<afxtempl.h>
#endif
#include<stdio.h>
#include<math.h>
#include "hsolv.h"
#include "hsolvDlg.h"
#include "mesh.h"
#include "spars.h"
#include "hsolvdoc.h"

#define CUTHILL_CLASS Chsolvdoc
#include "cuthill_impl.h"
