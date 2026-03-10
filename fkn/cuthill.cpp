// Cuthill-McKee algorithm — fkn solver wrapper
#include<stdafx.h>
#ifdef _WIN32
#include<afxtempl.h>
#endif
#include<stdio.h>
#include<math.h>
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "mesh.h"
#include "spars.h"
#include "FemmeDocCore.h"

#define CUTHILL_CLASS CFemmeDocCore
#define CUTHILL_HAS_AIR_GAP
#include "cuthill_impl.h"
