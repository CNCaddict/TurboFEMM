// Cuthill-McKee algorithm — csolv solver wrapper
#include<stdafx.h>
#ifdef _WIN32
#include<afxtempl.h>
#endif
#include<stdio.h>
#include<math.h>
#include "complex.h"
#include "csolv.h"
#include "csolvDlg.h"
#include "mesh.h"
#include "spars.h"
#include "FemmeDocCore.h"

#define CUTHILL_CLASS CFemmeDocCore
#include "cuthill_impl.h"
