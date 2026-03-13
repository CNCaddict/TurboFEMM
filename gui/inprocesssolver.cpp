// FEMM Qt 6 GUI — In-process solver implementation
#include "inprocesssolver.h"
#include "document.h"
#include "meshgen.h"
#include "resultsdoc.h"
#include "femm_types.h"

// Solver headers (from fkn_core library)
#include <stdafx.h>
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "mesh.h"
#include "spars.h"
#include "FemmeDocCore.h"

// GPU backend (persistent across solves)
#ifdef FEMM_USE_METAL
#include "gpu_backend.h"
#endif

// Lua (needed by solver for MagDirFctn evaluation)
#include "lua.h"
#include "lualib.h"
// The solver references this global (extern in prob1big.cpp, prob3big.cpp).
// We define it here since main.cpp (which normally defines it) is not in fkn_core.
lua_State *lua = nullptr;

#include <cstring>
#include <cstdlib>
#include <vector>

InProcessSolver::InProcessSolver(QObject *parent)
    : QObject(parent)
{
}

InProcessSolver::~InProcessSolver()
{
    // Clean up persistent Lua state
    if (m_lua) {
        lua_close(m_lua);
        m_lua = nullptr;
    }
    // m_gpu cleaned up automatically by unique_ptr
}

bool InProcessSolver::solve(FemmeDocument *doc, ResultsDocument *resultsOut)
{
    if (!doc) {
        m_lastError = "No document";
        return false;
    }

    // ---------------------------------------------------------------
    // Step 1: In-process mesh generation
    // ---------------------------------------------------------------
    emit progress("Meshing in-process...");

    MeshGenerator meshGen;
    connect(&meshGen, &MeshGenerator::progress, this, &InProcessSolver::progress);

    std::vector<MeshEdge> edges;
    if (!meshGen.generateMeshInProcess(doc, edges)) {
        m_lastError = "Mesh generation failed: " + meshGen.lastError();
        return false;
    }

    return solveExistingMesh(doc, edges, resultsOut);
}

bool InProcessSolver::solveExistingMesh(FemmeDocument *doc,
                                         const std::vector<MeshEdge> &edges,
                                         ResultsDocument *resultsOut)
{
    if (!doc) {
        m_lastError = "No document";
        return false;
    }

    // ---------------------------------------------------------------
    // Step 2: Prepare solver input (translate GUI→solver data)
    // ---------------------------------------------------------------
    emit progress("Preparing solver data...");

    int numMats = (int)doc->materialProps.size();
    int numBdry = (int)doc->boundaryProps.size();
    int numPts  = (int)doc->pointProps.size();
    int numCirc = (int)doc->circuitProps.size();

    // Filter out <No Mesh> labels — the file-based solver only reads non-hole
    // labels, and Triangle region attributes use filtered indices.  We must
    // match that convention so meshele[i].lbl indexes into labellist correctly.
    std::vector<int> blkMap;  // original index → filtered index
    blkMap.reserve(doc->blockLabels.size());
    for (const auto &blk : doc->blockLabels) {
        if (blk.blockType != "<No Mesh>")
            blkMap.push_back((int)blkMap.size());
        else
            blkMap.push_back(-1);  // hole — not sent to solver
    }
    int numBlk = 0;
    for (int m : blkMap) if (m >= 0) numBlk++;

    // Material properties
    std::vector<InMemMaterialProp> matProps(numMats);
    // Keep BH data alive for the duration of the solve
    std::vector<std::vector<double>> bhB(numMats), bhH(numMats);

    for (int i = 0; i < numMats; i++) {
        const FMaterialProp &src = doc->materialProps[i];
        InMemMaterialProp &dst = matProps[i];
        dst.mu_x = src.mu_x;
        dst.mu_y = src.mu_y;
        dst.H_c = src.H_c;
        dst.Theta_m = src.theta_m;
        dst.Jr = src.Jsrc.re;
        dst.Ji = src.Jsrc.im;
        dst.Cduct = src.Cduct;
        dst.Lam_d = src.Lam_d;
        dst.Theta_hn = src.Theta_hn;
        dst.Theta_hx = src.Theta_hx;
        dst.Theta_hy = src.Theta_hy;
        dst.LamFill = src.lamFill;
        dst.WireD = src.wireD;
        dst.LamType = src.lamType;
        dst.NStrands = src.nStrands;
        dst.BHpoints = src.bhPoints;
        dst.Bdata = nullptr;
        dst.Hdata_re = nullptr;
        if (src.bhPoints > 0 && (int)src.bhData.size() >= src.bhPoints) {
            bhB[i].resize(src.bhPoints);
            bhH[i].resize(src.bhPoints);
            for (int j = 0; j < src.bhPoints; j++) {
                bhB[i][j] = src.bhData[j].first;   // B value
                bhH[i][j] = src.bhData[j].second;   // H value (real)
            }
            dst.Bdata = bhB[i].data();
            dst.Hdata_re = bhH[i].data();
        }
    }

    // Boundary properties
    std::vector<InMemBoundaryProp> bdryProps(numBdry);
    for (int i = 0; i < numBdry; i++) {
        const FBoundaryProp &src = doc->boundaryProps[i];
        InMemBoundaryProp &dst = bdryProps[i];
        dst.BdryFormat = src.bdryFormat;
        dst.A0 = src.A0; dst.A1 = src.A1; dst.A2 = src.A2; dst.phi = src.phi;
        dst.Mu = src.Mu; dst.Sig = src.Sig;
        dst.c0_re = src.c0.re; dst.c0_im = src.c0.im;
        dst.c1_re = src.c1.re; dst.c1_im = src.c1.im;
        dst.InnerAngle = src.innerAngle;
        dst.OuterAngle = src.outerAngle;
    }

    // Point properties
    std::vector<InMemPointProp> ptProps(numPts);
    for (int i = 0; i < numPts; i++) {
        ptProps[i].Jr = doc->pointProps[i].Jp.re;
        ptProps[i].Ji = doc->pointProps[i].Jp.im;
        ptProps[i].Ar = doc->pointProps[i].Ap.re;
        ptProps[i].Ai = doc->pointProps[i].Ap.im;
    }

    // Circuit properties
    std::vector<InMemCircuit> circProps(numCirc);
    for (int i = 0; i < numCirc; i++) {
        // Note: GUI stores Amps in the circuit; solver uses dVolts for series, Amps for parallel
        // The .fem file convention: serial circuits store total amps * turns
        circProps[i].dVolts_re = 0.0;
        circProps[i].dVolts_im = 0.0;
        circProps[i].Amps_re = doc->circuitProps[i].amps.re;
        circProps[i].Amps_im = doc->circuitProps[i].amps.im;
        circProps[i].CircType = doc->circuitProps[i].circType;
    }

    // Block labels — resolve string names to indices, skip <No Mesh> holes
    std::vector<InMemBlockLabel> blkLabels(numBlk);
    std::vector<std::string> magDirStrs(numBlk);  // keep strings alive
    int bi = 0;
    for (int i = 0; i < (int)doc->blockLabels.size(); i++) {
        if (blkMap[i] < 0) continue;  // skip holes
        const FBlockLabel &src = doc->blockLabels[i];
        InMemBlockLabel &dst = blkLabels[bi];
        dst.x = src.x;
        dst.y = src.y;
        dst.MaxArea = src.maxArea;
        dst.MagDir = src.magDir;
        dst.BlockType = doc->findMaterialPropIndex(src.blockType);
        dst.InCircuit = (src.inCircuit == "<None>") ? -1 : doc->findCircuitPropIndex(src.inCircuit);
        dst.InGroup = src.inGroup;
        dst.Turns = src.turns;
        dst.IsExternal = src.isExternal ? 1 : 0;
        dst.IsDefault = src.isDefault ? 1 : 0;
        if (!src.magDirFctn.isEmpty()) {
            magDirStrs[bi] = src.magDirFctn.toStdString();
            dst.MagDirFctn = magDirStrs[bi].c_str();
        } else {
            dst.MagDirFctn = nullptr;
        }
        bi++;
    }

    // Mesh data for solver
    int numMeshNodes = (int)doc->meshNodes.size();
    int numMeshEls = (int)doc->meshElements.size();
    int numMeshEdges = (int)edges.size();

    std::vector<InMemMeshNode> meshNodes(numMeshNodes);
    for (int i = 0; i < numMeshNodes; i++) {
        meshNodes[i].x = doc->meshNodes[i].x;
        meshNodes[i].y = doc->meshNodes[i].y;
        meshNodes[i].boundaryMarker = doc->meshNodes[i].boundaryMarker;
    }

    std::vector<InMemMeshElement> meshEls(numMeshEls);
    for (int i = 0; i < numMeshEls; i++) {
        meshEls[i].p[0] = doc->meshElements[i].p[0];
        meshEls[i].p[1] = doc->meshElements[i].p[1];
        meshEls[i].p[2] = doc->meshElements[i].p[2];
        meshEls[i].label = doc->meshElements[i].label;
    }

    std::vector<InMemMeshEdge> meshEdges(numMeshEdges);
    for (int i = 0; i < numMeshEdges; i++) {
        meshEdges[i].n0 = edges[i].n0;
        meshEdges[i].n1 = edges[i].n1;
        meshEdges[i].marker = edges[i].marker;
    }

    // ---------------------------------------------------------------
    // Step 3: Initialize Lua state (required by solver)
    // ---------------------------------------------------------------
    // Reuse persistent Lua state across solves (saves ~5ms per step).
    // The Lua state is lightweight and stateless between solves.
    lua_State *prevLua = lua;
    if (!m_lua) {
        m_lua = lua_open(4096);
        lua_baselibopen(m_lua);
        lua_strlibopen(m_lua);
        lua_mathlibopen(m_lua);
        lua_iolibopen(m_lua);
    }
    lua = m_lua;

    // ---------------------------------------------------------------
    // Step 4: Run solver
    // ---------------------------------------------------------------
    emit progress("Running solver...");

    CFemmeDocCore Doc;
    CFknDlg dlg;
    Doc.TheView = &dlg;

    // Route solver progress callbacks to Qt signals so the GUI stays
    // informed during long PCG solves and nonlinear iterations.
    dlg.m_prog1.onProgress = [this](int pos) {
        emit progress(QString("PCG: %1%").arg(pos));
    };
    dlg.m_prog2.onProgress = [this](int pos) {
        emit progress(QString("Nonlinear convergence: %1%").arg(pos));
    };
    dlg.onStatusText = [this](int /*id*/, const char *text) {
        if (text && text[0])
            emit progress(QString::fromUtf8(text));
    };
    dlg.onLogMessage = [this](const char *text) {
        if (text && text[0])
            emit progress(QString::fromUtf8(text));
    };

    // Load document data
    if (!Doc.LoadFromDocument(
            doc->frequency, doc->precision, 1.0 /* relax */,
            (int)doc->lengthUnits, doc->acSolver,
            (doc->problemType == ProblemType::Axisymmetric) ? 1 : 0,
            (doc->coordType == CoordType::Polar) ? 1 : 0,
            doc->extZo, doc->extRo, doc->extRi,
            matProps.data(), numMats,
            bdryProps.data(), numBdry,
            ptProps.data(), numPts,
            circProps.data(), numCirc,
            blkLabels.data(), numBlk)) {
        m_lastError = "Failed to load document into solver";
        Doc.CleanUp();
        lua = prevLua;
        return false;
    }

    // Load mesh data
    if (!Doc.LoadMeshFromMemory(
            meshNodes.data(), numMeshNodes,
            meshEls.data(), numMeshEls,
            meshEdges.data(), numMeshEdges)) {
        m_lastError = "Failed to load mesh into solver";
        Doc.CleanUp();
        lua = prevLua;
        return false;
    }

    // Cuthill-McKee renumbering (in-memory — no .edge file)
    if (!Doc.CuthillFromMemory(meshEdges.data(), numMeshEdges)) {
        m_lastError = "Failed to renumber nodes";
        Doc.CleanUp();
        lua = prevLua;
        return false;
    }

    emit progress(QString("Solving: %1 nodes, %2 elements")
                  .arg(Doc.NumNodes).arg(Doc.NumEls));

    bool solveOk = false;

    // ---------------------------------------------------------------
    // Step 4b: Ensure persistent GPU backend exists
    // ---------------------------------------------------------------
#ifdef FEMM_USE_METAL
    if (!m_gpu) {
        m_gpu = GPUBackend::create();
    }
#endif

    if (Doc.Frequency == 0) {
        // Static solve
        CBigLinProb L;
        L.TheView = &dlg;
        L.Precision = Doc.Precision;
#ifdef FEMM_USE_METAL
        L.externalGPU = m_gpu.get();
#endif

        if (!L.Create(Doc.NumNodes, Doc.BandWidth)) {
            m_lastError = "Couldn't allocate solver matrices";
            Doc.CleanUp();
            lua = prevLua;
            return false;
        }

        if (Doc.ProblemType == FALSE) {
            solveOk = Doc.Static2D(L);
        } else {
            solveOk = Doc.StaticAxisymmetric(L);
        }

        if (!solveOk) {
            m_lastError = "Solver failed";
            Doc.CleanUp();
            lua = prevLua;
            return false;
        }

        // ---------------------------------------------------------------
        // Step 5: Extract results into ResultsDocument
        // ---------------------------------------------------------------
        emit progress("Extracting results...");

        // Prepare arrays for loadFromSolverData
        std::vector<QString> matNames(numMats);
        std::vector<double> rMu_x(numMats), rMu_y(numMats), rH_c(numMats), rCduct(numMats);
        std::vector<int> rLamType(numMats);
        std::vector<double> rLamFill(numMats), rLam_d(numMats);
        std::vector<int> rBhPts(numMats);
        std::vector<const double *> rBdata(numMats, nullptr), rHdata(numMats, nullptr);
        // Keep temporary H arrays alive
        std::vector<std::vector<double>> rHreal(numMats);

        for (int i = 0; i < numMats; i++) {
            matNames[i] = doc->materialProps[i].blockName;
            rMu_x[i] = Doc.blockproplist[i].mu_x;
            rMu_y[i] = Doc.blockproplist[i].mu_y;
            rH_c[i] = Doc.blockproplist[i].H_c;
            rCduct[i] = Doc.blockproplist[i].Cduct;
            rLamType[i] = Doc.blockproplist[i].LamType;
            rLamFill[i] = Doc.blockproplist[i].LamFill;
            rLam_d[i] = Doc.blockproplist[i].Lam_d;
            rBhPts[i] = Doc.blockproplist[i].BHpoints;
            if (Doc.blockproplist[i].BHpoints > 0) {
                rBdata[i] = Doc.blockproplist[i].Bdata;
                rHreal[i].resize(Doc.blockproplist[i].BHpoints);
                for (int j = 0; j < Doc.blockproplist[i].BHpoints; j++)
                    rHreal[i][j] = Doc.blockproplist[i].Hdata[j].re;
                rHdata[i] = rHreal[i].data();
            }
        }

        // Labels — use original block labels from GUI
        int rNumLabels = Doc.NumBlockLabels;
        std::vector<double> rLblX(rNumLabels), rLblY(rNumLabels), rLblMagDir(rNumLabels);
        std::vector<int> rLblBlockType(rNumLabels), rLblInGroup(rNumLabels), rLblTurns(rNumLabels);
        for (int i = 0; i < rNumLabels; i++) {
            rLblX[i] = Doc.labellist[i].x;
            rLblY[i] = Doc.labellist[i].y;
            rLblBlockType[i] = Doc.labellist[i].BlockType;
            rLblMagDir[i] = Doc.labellist[i].MagDir;
            rLblInGroup[i] = Doc.labellist[i].InGroup;
            rLblTurns[i] = Doc.labellist[i].Turns;
        }

        // Nodes: solver stores in cm
        // After Static2D, L.b[i] = L.V[i]*c where c=PI*4e-05 — this is
        // the vector potential A in proper units (same as .ans file output).
        std::vector<double> rNodeX(Doc.NumNodes), rNodeY(Doc.NumNodes);
        std::vector<double> rNodeA_re(Doc.NumNodes);
        for (int i = 0; i < Doc.NumNodes; i++) {
            rNodeX[i] = Doc.meshnode[i].x;      // in cm
            rNodeY[i] = Doc.meshnode[i].y;      // in cm
            rNodeA_re[i] = L.b[i];              // A in proper units
        }

        // Elements
        std::vector<int> rElmP(Doc.NumEls * 3), rElmLbl(Doc.NumEls), rElmBlk(Doc.NumEls);
        for (int i = 0; i < Doc.NumEls; i++) {
            rElmP[i*3]   = Doc.meshele[i].p[0];
            rElmP[i*3+1] = Doc.meshele[i].p[1];
            rElmP[i*3+2] = Doc.meshele[i].p[2];
            rElmLbl[i] = Doc.meshele[i].lbl;
            rElmBlk[i] = Doc.meshele[i].blk;
        }

        if (!resultsOut->loadFromSolverData(
                Doc.ProblemType, Doc.LengthUnits, Doc.Frequency, doc->depth,
                numMats, matNames.data(),
                rMu_x.data(), rMu_y.data(), rH_c.data(), rCduct.data(),
                rLamType.data(), rLamFill.data(), rLam_d.data(),
                rBhPts.data(), rBdata.data(), rHdata.data(),
                rNumLabels, rLblX.data(), rLblY.data(),
                rLblBlockType.data(), rLblMagDir.data(), rLblInGroup.data(), rLblTurns.data(),
                Doc.NumNodes, rNodeX.data(), rNodeY.data(),
                rNodeA_re.data(), nullptr /* nodeA_im=0 for static */,
                Doc.NumEls, rElmP.data(), rElmLbl.data(), rElmBlk.data())) {
            m_lastError = "Failed to extract results";
            Doc.CleanUp();
            lua = prevLua;
            return false;
        }

        // Copy Steinmetz loss coefficients from GUI material props to results
        for (int i = 0; i < numMats && i < (int)resultsOut->materials.size(); i++) {
            resultsOut->materials[i].Kh = doc->materialProps[i].Kh;
            resultsOut->materials[i].Kc = doc->materialProps[i].Kc;
            resultsOut->materials[i].Ke = doc->materialProps[i].Ke;
            resultsOut->materials[i].alpha_loss = doc->materialProps[i].alpha_loss;
            resultsOut->materials[i].density = doc->materialProps[i].density;
        }
        // Copy block label loss settings to results
        for (int i = 0; i < rNumLabels && i < (int)resultsOut->labels.size(); i++) {
            if (i < (int)doc->blockLabels.size()) {
                resultsOut->labels[i].calculateLosses = doc->blockLabels[i].calculateLosses;
            }
        }
    } else {
        // Harmonic solve
        CBigComplexLinProb L;
        L.TheView = &dlg;
        L.Precision = Doc.Precision;

        if (!L.Create(Doc.NumNodes + Doc.NumCircProps, Doc.BandWidth, Doc.NumNodes)) {
            m_lastError = "Couldn't allocate solver matrices";
            Doc.CleanUp();
            lua = prevLua;
            return false;
        }

        if (Doc.ProblemType == FALSE) {
            solveOk = Doc.Harmonic2D(L);
        } else {
            solveOk = Doc.HarmonicAxisymmetric(L);
        }

        if (!solveOk) {
            m_lastError = "Solver failed";
            Doc.CleanUp();
            lua = prevLua;
            return false;
        }

        // Extract harmonic results — similar to static but with complex A
        // TODO: implement harmonic results extraction
        m_lastError = "Harmonic in-process results extraction not yet implemented";
        Doc.CleanUp();
        lua = prevLua;
        return false;
    }

    Doc.CleanUp();
    lua = prevLua;

    emit progress("Solve complete.");
    return true;
}
