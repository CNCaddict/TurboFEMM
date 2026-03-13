// FEMM Qt 6 GUI — Document data model + .fem file I/O
#include "document.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

FemmeDocument::FemmeDocument(QObject *parent)
    : QObject(parent)
{
}

FemmeDocument::~FemmeDocument() = default;

// ---------------------------------------------------------------
// .fem parsing helpers
// ---------------------------------------------------------------

QString FemmeDocument::stripKey(const QString &line)
{
    int idx = line.indexOf('=');
    if (idx < 0) return QString();
    return line.mid(idx + 1).trimmed();
}

int FemmeDocument::findPointPropIndex(const QString &name) const
{
    for (int i = 0; i < (int)pointProps.size(); i++)
        if (pointProps[i].pointName == name) return i;
    return -1;
}

int FemmeDocument::findBoundaryPropIndex(const QString &name) const
{
    for (int i = 0; i < (int)boundaryProps.size(); i++)
        if (boundaryProps[i].bdryName == name) return i;
    return -1;
}

int FemmeDocument::findMaterialPropIndex(const QString &name) const
{
    for (int i = 0; i < (int)materialProps.size(); i++)
        if (materialProps[i].blockName == name) return i;
    return -1;
}

int FemmeDocument::findCircuitPropIndex(const QString &name) const
{
    for (int i = 0; i < (int)circuitProps.size(); i++)
        if (circuitProps[i].circName == name) return i;
    return -1;
}

QString FemmeDocument::pointPropName(int idx) const
{
    if (idx < 0 || idx >= (int)pointProps.size()) return QStringLiteral("<None>");
    return pointProps[idx].pointName;
}

QString FemmeDocument::boundaryPropName(int idx) const
{
    if (idx < 0 || idx >= (int)boundaryProps.size()) return QStringLiteral("<None>");
    return boundaryProps[idx].bdryName;
}

QString FemmeDocument::materialPropName(int idx) const
{
    if (idx < 0 || idx >= (int)materialProps.size()) return QStringLiteral("<None>");
    return materialProps[idx].blockName;
}

QString FemmeDocument::circuitPropName(int idx) const
{
    if (idx < 0 || idx >= (int)circuitProps.size()) return QStringLiteral("<None>");
    return circuitProps[idx].circName;
}

// Helper: extract quoted string from value part
static QString parseQuotedString(const QString &val)
{
    int n1 = val.indexOf('"');
    if (n1 < 0) return val.trimmed();
    int n2 = val.indexOf('"', n1 + 1);
    if (n2 < 0) return val.mid(n1 + 1).trimmed();
    return val.mid(n1 + 1, n2 - n1 - 1);
}

// ---------------------------------------------------------------
// Load .fem file (magnetics format)
// ---------------------------------------------------------------

bool FemmeDocument::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);

    // Reset
    nodes.clear();
    segments.clear();
    arcSegments.clear();
    blockLabels.clear();
    pointProps.clear();
    boundaryProps.clear();
    materialProps.clear();
    circuitProps.clear();
    meshNodes.clear();
    meshElements.clear();
    meshEdges.clear();
    hasMesh = false;

    // Defaults
    frequency = 0.0;
    precision = 1.0e-8;
    minAngle = 30.0;
    depth = 1.0;
    smartMesh = 1;
    acSolver = 0;
    problemType = ProblemType::Planar;
    lengthUnits = LengthUnits::Inches;
    coordType = CoordType::Cartesian;
    extZo = extRo = extRi = 0.0;
    comment.clear();
    prevSoln.clear();
    prevType = 0;

    // Temp property accumulators
    FPointProp pprop;
    FBoundaryProp bprop;
    FMaterialProp mprop;
    FCircuit cprop;

    while (!in.atEnd()) {
        QString line = in.readLine();
        QString q = line.trimmed();
        if (q.isEmpty()) continue;

        // Extract first token (lowercase for comparison)
        QString token = q.split(QRegularExpression("\\s+")).first().toLower();
        QString val = stripKey(q);

        // ---- Header fields ----
        if (token == "[frequency]") {
            frequency = std::fabs(val.toDouble());
        }
        else if (token == "[precision]") {
            precision = val.toDouble();
        }
        else if (token == "[minangle]") {
            minAngle = val.toDouble();
        }
        else if (token == "[depth]") {
            depth = val.toDouble();
        }
        else if (token == "[dosmartmesh]") {
            smartMesh = val.toInt();
        }
        else if (token == "[acsolver]") {
            acSolver = val.toInt();
        }
        else if (token == "[lengthunits]") {
            QString u = val.toLower().trimmed();
            if (u.startsWith("inches"))       lengthUnits = LengthUnits::Inches;
            else if (u.startsWith("millimeters")) lengthUnits = LengthUnits::Millimeters;
            else if (u.startsWith("centimeters")) lengthUnits = LengthUnits::Centimeters;
            else if (u.startsWith("meters"))      lengthUnits = LengthUnits::Meters;
            else if (u.startsWith("mils"))         lengthUnits = LengthUnits::Mils;
            else if (u.startsWith("microns"))      lengthUnits = LengthUnits::Microns;
        }
        else if (token == "[problemtype]") {
            QString pt = val.toLower().trimmed();
            if (pt.startsWith("planar")) problemType = ProblemType::Planar;
            else if (pt.startsWith("axi"))  problemType = ProblemType::Axisymmetric;
        }
        else if (token == "[coordinates]") {
            QString ct = val.toLower().trimmed();
            if (ct.startsWith("cart")) coordType = CoordType::Cartesian;
            else if (ct.startsWith("polar")) coordType = CoordType::Polar;
        }
        else if (token == "[extzo]") { extZo = val.toDouble(); }
        else if (token == "[extro]") { extRo = val.toDouble(); }
        else if (token == "[extri]") { extRi = val.toDouble(); }
        else if (token == "[comment]") {
            comment = parseQuotedString(val);
        }
        else if (token == "[prevsoln]") {
            prevSoln = parseQuotedString(val);
        }
        else if (token == "[prevtype]") {
            prevType = val.toInt();
        }

        // ---- Point Properties ----
        else if (token == "[pointprops]") {
            // count only, we accumulate with begin/end
        }
        else if (token == "<beginpoint>") {
            pprop = FPointProp();
        }
        else if (token == "<pointname>") {
            pprop.pointName = parseQuotedString(val);
        }
        else if (token == "<a_re>") { pprop.Ap.re = val.toDouble(); }
        else if (token == "<a_im>") { pprop.Ap.im = val.toDouble(); }
        else if (token == "<i_re>") { pprop.Jp.re = val.toDouble(); }
        else if (token == "<i_im>") { pprop.Jp.im = val.toDouble(); }
        // GUI format uses vpr/vpi/qpr/qpi
        else if (token == "<vpr>") { pprop.Ap.re = val.toDouble(); }
        else if (token == "<vpi>") { pprop.Ap.im = val.toDouble(); }
        else if (token == "<qpr>") { pprop.Jp.re = val.toDouble(); }
        else if (token == "<qpi>") { pprop.Jp.im = val.toDouble(); }
        else if (token == "<endpoint>") {
            pointProps.push_back(pprop);
        }

        // ---- Boundary Properties ----
        else if (token == "[bdryprops]") {
            // count only
        }
        else if (token == "<beginbdry>") {
            bprop = FBoundaryProp();
        }
        else if (token == "<bdryname>") {
            bprop.bdryName = parseQuotedString(val);
        }
        else if (token == "<bdrytype>") { bprop.bdryFormat = val.toInt(); }
        else if (token == "<a_0>") { bprop.A0 = val.toDouble(); }
        else if (token == "<a_1>") { bprop.A1 = val.toDouble(); }
        else if (token == "<a_2>") { bprop.A2 = val.toDouble(); }
        else if (token == "<phi>") { bprop.phi = val.toDouble(); }
        else if (token == "<mu_ssd>") { bprop.Mu = val.toDouble(); }
        else if (token == "<sigma_ssd>") { bprop.Sig = val.toDouble(); }
        else if (token == "<c0>") { bprop.c0.re = val.toDouble(); }
        else if (token == "<c0i>") { bprop.c0.im = val.toDouble(); }
        else if (token == "<c1>") { bprop.c1.re = val.toDouble(); }
        else if (token == "<c1i>") { bprop.c1.im = val.toDouble(); }
        else if (token == "<innerangle>") { bprop.innerAngle = val.toDouble(); }
        else if (token == "<outerangle>") { bprop.outerAngle = val.toDouble(); }
        // GUI format uses vsr/vsi/qsr/qsi
        else if (token == "<vsr>") { bprop.A0 = val.toDouble(); }
        else if (token == "<vsi>") { bprop.A1 = val.toDouble(); }
        else if (token == "<qsr>") { bprop.c0.re = val.toDouble(); }
        else if (token == "<qsi>") { bprop.c0.im = val.toDouble(); }
        else if (token == "<c0r>") { bprop.c0.re = val.toDouble(); }
        else if (token == "<c1r>") { bprop.c1.re = val.toDouble(); }
        else if (token == "<endbdry>") {
            boundaryProps.push_back(bprop);
        }

        // ---- Block (Material) Properties ----
        else if (token == "[blockprops]") {
            // count only
        }
        else if (token == "<beginblock>") {
            mprop = FMaterialProp();
        }
        else if (token == "<blockname>") {
            mprop.blockName = parseQuotedString(val);
        }
        else if (token == "<mu_x>") { mprop.mu_x = val.toDouble(); }
        else if (token == "<mu_y>") { mprop.mu_y = val.toDouble(); }
        else if (token == "<h_c>") { mprop.H_c = val.toDouble(); }
        else if (token == "<h_cangle>") { mprop.theta_m = val.toDouble(); }
        else if (token == "<j_re>") { mprop.Jsrc.re = val.toDouble(); }
        else if (token == "<j_im>") { mprop.Jsrc.im = val.toDouble(); }
        else if (token == "<sigma>") { mprop.Cduct = val.toDouble(); }
        else if (token == "<d_lam>") { mprop.Lam_d = val.toDouble(); }
        else if (token == "<phi_h>") { mprop.Theta_hn = val.toDouble(); }
        else if (token == "<phi_hx>") { mprop.Theta_hx = val.toDouble(); }
        else if (token == "<phi_hy>") { mprop.Theta_hy = val.toDouble(); }
        else if (token == "<lamtype>") { mprop.lamType = val.toInt(); }
        else if (token == "<lamfill>") { mprop.lamFill = val.toDouble(); }
        else if (token == "<nstrands>") { mprop.nStrands = val.toInt(); }
        else if (token == "<wired>") { mprop.wireD = val.toDouble(); }
        else if (token == "<bhpoints>") {
            int npts = val.toInt();
            mprop.bhPoints = npts;
            mprop.bhData.clear();
            for (int j = 0; j < npts && !in.atEnd(); j++) {
                QString bhLine = in.readLine().trimmed();
                QStringList parts = bhLine.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    double B = parts[0].toDouble();
                    double H = parts[1].toDouble();
                    mprop.bhData.push_back({B, H});
                }
            }
        }
        // Steinmetz iron loss coefficients
        else if (token == "<kh>") { mprop.Kh = val.toDouble(); }
        else if (token == "<kc>") { mprop.Kc = val.toDouble(); }
        else if (token == "<ke>") { mprop.Ke = val.toDouble(); }
        else if (token == "<alpha_loss>") { mprop.alpha_loss = val.toDouble(); }
        else if (token == "<density>") { mprop.density = val.toDouble(); }
        else if (token == "<corelosspoints>") {
            int npts = val.toInt();
            mprop.coreLossData.clear();
            for (int j = 0; j < npts && !in.atEnd(); j++) {
                QString clLine = in.readLine().trimmed();
                QStringList parts = clLine.split(QRegularExpression("\\s+"));
                if (parts.size() >= 3) {
                    CoreLossPoint pt;
                    pt.B = parts[0].toDouble();
                    pt.freq = parts[1].toDouble();
                    pt.loss_Wkg = parts[2].toDouble();
                    mprop.coreLossData.push_back(pt);
                }
            }
        }
        else if (token == "<endblock>") {
            materialProps.push_back(mprop);
        }

        // ---- Circuit Properties ----
        else if (token == "[circuitprops]") {
            // count only
        }
        // Also handle conductor format
        else if (token == "[conductorprops]") {
            // count only
        }
        else if (token == "<begincircuit>" || token == "<beginconductor>") {
            cprop = FCircuit();
        }
        else if (token == "<circuitname>" || token == "<conductorname>") {
            cprop.circName = parseQuotedString(val);
        }
        else if (token == "<totalamps_re>") { cprop.amps.re = val.toDouble(); }
        else if (token == "<totalamps_im>") { cprop.amps.im = val.toDouble(); }
        else if (token == "<circuittype>" || token == "<conductortype>") {
            cprop.circType = val.toInt();
        }
        // GUI format uses vcr/vci/qcr/qci
        else if (token == "<vcr>") { /* voltage, not used in magnetics */ }
        else if (token == "<vci>") { }
        else if (token == "<qcr>") { }
        else if (token == "<qci>") { }
        else if (token == "<endcircuit>" || token == "<endconductor>") {
            circuitProps.push_back(cprop);
        }

        // ---- Geometry: Nodes ----
        else if (token == "[numpoints]") {
            int k = val.toInt();
            nodes.reserve(k);
            for (int i = 0; i < k && !in.atEnd(); i++) {
                QString nline = in.readLine().trimmed();
                QStringList parts = nline.split(QRegularExpression("[\\t\\s]+"));
                FNode nd;
                if (parts.size() >= 2) {
                    nd.x = parts[0].toDouble();
                    nd.y = parts[1].toDouble();
                }
                // boundary marker index (1-based, 0=none)
                int bm = 0;
                if (parts.size() >= 3) bm = parts[2].toInt();
                if (bm > 0) nd.boundaryMarker = pointPropName(bm - 1);
                // group
                if (parts.size() >= 4) nd.inGroup = parts[3].toInt();
                // optional user name (quoted string in remainder)
                if (nline.contains('"')) {
                    nd.name = parseQuotedString(nline);
                }
                nodes.push_back(nd);
            }
        }

        // ---- Geometry: Segments ----
        else if (token == "[numsegments]") {
            int k = val.toInt();
            segments.reserve(k);
            for (int i = 0; i < k && !in.atEnd(); i++) {
                QString sline = in.readLine().trimmed();
                QStringList parts = sline.split(QRegularExpression("[\\t\\s]+"));
                FSegment seg;
                if (parts.size() >= 2) {
                    seg.n0 = parts[0].toInt();
                    seg.n1 = parts[1].toInt();
                }
                if (parts.size() >= 3)
                    seg.maxSideLength = parts[2].toDouble();
                // boundary marker index (1-based)
                int bm = 0;
                if (parts.size() >= 4) bm = parts[3].toInt();
                if (bm > 0) seg.boundaryMarker = boundaryPropName(bm - 1);
                if (parts.size() >= 5) seg.hidden = (parts[4].toInt() != 0);
                if (parts.size() >= 6) seg.inGroup = parts[5].toInt();
                segments.push_back(seg);
            }
        }

        // ---- Geometry: Arc Segments ----
        else if (token == "[numarcsegments]") {
            int k = val.toInt();
            arcSegments.reserve(k);
            for (int i = 0; i < k && !in.atEnd(); i++) {
                QString aline = in.readLine().trimmed();
                QStringList parts = aline.split(QRegularExpression("[\\t\\s]+"));
                FArcSegment arc;
                if (parts.size() >= 2) {
                    arc.n0 = parts[0].toInt();
                    arc.n1 = parts[1].toInt();
                }
                if (parts.size() >= 3) arc.arcLength = parts[2].toDouble();
                if (parts.size() >= 4) arc.maxSideLength = parts[3].toDouble();
                // boundary marker index (1-based)
                int bm = 0;
                if (parts.size() >= 5) bm = parts[4].toInt();
                if (bm > 0) arc.boundaryMarker = boundaryPropName(bm - 1);
                if (parts.size() >= 6) arc.hidden = (parts[5].toInt() != 0);
                if (parts.size() >= 7) arc.inGroup = parts[6].toInt();
                arcSegments.push_back(arc);
            }
        }

        // ---- Holes (block labels with no mesh) ----
        else if (token == "[numholes]") {
            int k = val.toInt();
            // holes are block labels with blockType = "<No Mesh>"
            for (int i = 0; i < k && !in.atEnd(); i++) {
                QString hline = in.readLine().trimmed();
                QStringList parts = hline.split(QRegularExpression("[\\t\\s]+"));
                FBlockLabel blk;
                if (parts.size() >= 2) {
                    blk.x = parts[0].toDouble();
                    blk.y = parts[1].toDouble();
                }
                blk.blockType = QStringLiteral("<No Mesh>");
                if (parts.size() >= 3) blk.inGroup = parts[2].toInt();
                blockLabels.push_back(blk);
            }
        }

        // ---- Geometry: Block Labels ----
        else if (token == "[numblocklabels]") {
            int k = val.toInt();
            blockLabels.reserve(blockLabels.size() + k);
            for (int i = 0; i < k && !in.atEnd(); i++) {
                QString bline = in.readLine().trimmed();
                QStringList parts = bline.split(QRegularExpression("[\\t\\s]+"));
                FBlockLabel blk;
                if (parts.size() >= 2) {
                    blk.x = parts[0].toDouble();
                    blk.y = parts[1].toDouble();
                }
                // block type index (1-based)
                int bt = 0;
                if (parts.size() >= 3) bt = parts[2].toInt();
                if (bt > 0) blk.blockType = materialPropName(bt - 1);

                // max area (stored as diameter in file, convert to area)
                if (parts.size() >= 4) {
                    double d = parts[3].toDouble();
                    if (d > 0)
                        blk.maxArea = M_PI * d * d / 4.0;
                    else
                        blk.maxArea = 0.0;
                }

                // circuit index (1-based)
                if (parts.size() >= 5) {
                    int ci = parts[4].toInt();
                    if (ci > 0) blk.inCircuit = circuitPropName(ci - 1);
                }

                if (parts.size() >= 6) {
                    blk.magDir = parts[5].toDouble();
                    // Normalize to [0, 360) — older files may have accumulated angles
                    blk.magDir = std::fmod(blk.magDir, 360.0);
                    if (blk.magDir < 0) blk.magDir += 360.0;
                }
                if (parts.size() >= 7) blk.inGroup = parts[6].toInt();
                if (parts.size() >= 8) blk.turns = parts[7].toInt();

                // external + default + calculateLosses flags packed
                if (parts.size() >= 9) {
                    int flags = parts[8].toInt();
                    blk.isExternal = (flags & 1) != 0;
                    blk.isDefault = (flags & 2) != 0;
                    blk.calculateLosses = (flags & 4) != 0;
                }

                // Fields 9-10 were lamThickness/stackingFactor in earlier versions;
                // now these live on the material.  Skip them but keep parsing
                // to find the correct quotedStart for name/magDirFctn.

                // optional quoted strings: magDirFctn and/or name
                // Find the first field that starts with a quote
                int quotedStart = 9; // default (old format)
                if (parts.size() >= 10 && !parts[9].startsWith('"'))
                    quotedStart = 11; // new format with lamThickness/stackingFactor

                if (parts.size() > quotedStart) {
                    QString remainder;
                    for (int p = quotedStart; p < parts.size(); p++) {
                        if (!remainder.isEmpty()) remainder += ' ';
                        remainder += parts[p];
                    }
                    // Extract all quoted strings from remainder
                    QList<QString> quotedStrings;
                    int pos = 0;
                    while (pos < remainder.size()) {
                        int q1 = remainder.indexOf('"', pos);
                        if (q1 < 0) break;
                        int q2 = remainder.indexOf('"', q1 + 1);
                        if (q2 < 0) break;
                        quotedStrings.append(remainder.mid(q1 + 1, q2 - q1 - 1));
                        pos = q2 + 1;
                    }
                    if (quotedStrings.size() >= 1)
                        blk.magDirFctn = quotedStrings[0];
                    if (quotedStrings.size() >= 2)
                        blk.name = quotedStrings[1];
                }

                blockLabels.push_back(blk);
            }
        }
    }

    file.close();
    m_filePath = path;
    isModified = false;
    return true;
}

// ---------------------------------------------------------------
// Save .fem file (magnetics format)
// ---------------------------------------------------------------

bool FemmeDocument::saveToFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setRealNumberPrecision(17);

    // Header
    out << "[Format]      =  1\n";
    out << "[Frequency]   =  " << frequency << "\n";
    out << "[Precision]   =  " << precision << "\n";
    out << "[MinAngle]    =  " << minAngle << "\n";
    out << "[DoSmartMesh] =  " << smartMesh << "\n";
    out << "[Depth]       =  " << depth << "\n";

    out << "[LengthUnits] =  ";
    switch (lengthUnits) {
    case LengthUnits::Millimeters: out << "millimeters"; break;
    case LengthUnits::Centimeters: out << "centimeters"; break;
    case LengthUnits::Meters:      out << "meters"; break;
    case LengthUnits::Mils:        out << "mils"; break;
    case LengthUnits::Microns:     out << "microns"; break;
    default:                       out << "inches"; break;
    }
    out << "\n";

    out << "[ProblemType] =  ";
    out << (problemType == ProblemType::Axisymmetric ? "axisymmetric" : "planar") << "\n";

    if (problemType == ProblemType::Axisymmetric && (extRo != 0.0 || extRi != 0.0)) {
        out << "[extZo] = " << extZo << "\n";
        out << "[extRo] = " << extRo << "\n";
        out << "[extRi] = " << extRi << "\n";
    }

    out << "[Coordinates] =  ";
    out << (coordType == CoordType::Polar ? "polar" : "cartesian") << "\n";

    if (acSolver != 0)
        out << "[ACSolver]    =  " << acSolver << "\n";

    if (!comment.isEmpty()) {
        QString escaped = comment;
        escaped.replace('\r', "\\r");
        escaped.replace('\n', "\\n");
        out << "[Comment]     =  \"" << escaped << "\"\n";
    }

    if (!prevSoln.isEmpty())
        out << "[PrevSoln]    =  \"" << prevSoln << "\"\n";
    if (prevType != 0)
        out << "[PrevType]    =  " << prevType << "\n";

    // Point properties
    out << "[PointProps]   = " << pointProps.size() << "\n";
    for (const auto &pp : pointProps) {
        out << "  <BeginPoint>\n";
        out << "    <PointName> = \"" << pp.pointName << "\"\n";
        out << "    <I_re> = " << pp.Jp.re << "\n";
        out << "    <I_im> = " << pp.Jp.im << "\n";
        out << "    <A_re> = " << pp.Ap.re << "\n";
        out << "    <A_im> = " << pp.Ap.im << "\n";
        out << "  <EndPoint>\n";
    }

    // Boundary properties
    out << "[BdryProps]   = " << boundaryProps.size() << "\n";
    for (const auto &bp : boundaryProps) {
        out << "  <BeginBdry>\n";
        out << "    <BdryName> = \"" << bp.bdryName << "\"\n";
        out << "    <BdryType> = " << bp.bdryFormat << "\n";
        out << "    <A_0> = " << bp.A0 << "\n";
        out << "    <A_1> = " << bp.A1 << "\n";
        out << "    <A_2> = " << bp.A2 << "\n";
        out << "    <phi> = " << bp.phi << "\n";
        out << "    <Mu_ssd> = " << bp.Mu << "\n";
        out << "    <Sigma_ssd> = " << bp.Sig << "\n";
        out << "    <c0> = " << bp.c0.re << "\n";
        out << "    <c0i> = " << bp.c0.im << "\n";
        out << "    <c1> = " << bp.c1.re << "\n";
        out << "    <c1i> = " << bp.c1.im << "\n";
        out << "    <InnerAngle> = " << bp.innerAngle << "\n";
        out << "    <OuterAngle> = " << bp.outerAngle << "\n";
        out << "  <EndBdry>\n";
    }

    // Block (material) properties
    out << "[BlockProps]  = " << materialProps.size() << "\n";
    for (const auto &mp : materialProps) {
        out << "  <BeginBlock>\n";
        out << "    <BlockName> = \"" << mp.blockName << "\"\n";
        out << "    <mu_x> = " << mp.mu_x << "\n";
        out << "    <mu_y> = " << mp.mu_y << "\n";
        out << "    <H_c> = " << mp.H_c << "\n";
        out << "    <H_cAngle> = " << mp.theta_m << "\n";
        out << "    <J_re> = " << mp.Jsrc.re << "\n";
        out << "    <J_im> = " << mp.Jsrc.im << "\n";
        out << "    <sigma> = " << mp.Cduct << "\n";
        out << "    <d_lam> = " << mp.Lam_d << "\n";
        out << "    <phi_h> = " << mp.Theta_hn << "\n";
        out << "    <phi_hx> = " << mp.Theta_hx << "\n";
        out << "    <phi_hy> = " << mp.Theta_hy << "\n";
        out << "    <LamType> = " << mp.lamType << "\n";
        out << "    <LamFill> = " << mp.lamFill << "\n";
        out << "    <NStrands> = " << mp.nStrands << "\n";
        out << "    <WireD> = " << mp.wireD << "\n";
        out << "    <BHPoints> = " << mp.bhPoints << "\n";
        for (const auto &bh : mp.bhData) {
            out << "      " << bh.first << "\t" << bh.second << "\n";
        }
        // Steinmetz iron loss coefficients (only write if any are set)
        if (mp.Kh != 0.0 || mp.Kc != 0.0 || mp.Ke != 0.0) {
            out << "    <Kh> = " << mp.Kh << "\n";
            out << "    <Kc> = " << mp.Kc << "\n";
            out << "    <Ke> = " << mp.Ke << "\n";
            out << "    <alpha_loss> = " << mp.alpha_loss << "\n";
        }
        if (mp.density != 0.0)
            out << "    <density> = " << mp.density << "\n";
        if (!mp.coreLossData.empty()) {
            out << "    <CoreLossPoints> = " << (int)mp.coreLossData.size() << "\n";
            for (const auto &cl : mp.coreLossData) {
                out << "      " << cl.B << "\t" << cl.freq << "\t" << cl.loss_Wkg << "\n";
            }
        }
        out << "  <EndBlock>\n";
    }

    // Circuit properties
    out << "[CircuitProps]  = " << circuitProps.size() << "\n";
    for (const auto &cp : circuitProps) {
        out << "  <BeginCircuit>\n";
        out << "    <CircuitName> = \"" << cp.circName << "\"\n";
        out << "    <TotalAmps_re> = " << cp.amps.re << "\n";
        out << "    <TotalAmps_im> = " << cp.amps.im << "\n";
        out << "    <CircuitType> = " << cp.circType << "\n";
        out << "  <EndCircuit>\n";
    }

    // Nodes
    out << "[NumPoints] = " << nodes.size() << "\n";
    for (const auto &nd : nodes) {
        // Point property index (1-based, 0 = none)
        int ppIdx = 0;
        if (nd.boundaryMarker != "<None>") {
            int idx = findPointPropIndex(nd.boundaryMarker);
            if (idx >= 0) ppIdx = idx + 1;
        }
        out << nd.x << "\t" << nd.y << "\t" << ppIdx << "\t" << nd.inGroup;
        if (!nd.name.isEmpty())
            out << "\t\"" << nd.name << "\"";
        out << "\n";
    }

    // Segments
    out << "[NumSegments] = " << segments.size() << "\n";
    for (const auto &seg : segments) {
        int bpIdx = 0;
        if (seg.boundaryMarker != "<None>") {
            int idx = findBoundaryPropIndex(seg.boundaryMarker);
            if (idx >= 0) bpIdx = idx + 1;
        }
        out << seg.n0 << "\t" << seg.n1 << "\t";
        if (seg.maxSideLength < 0) out << "-1";
        else out << seg.maxSideLength;
        out << "\t" << bpIdx << "\t" << (seg.hidden ? 1 : 0) << "\t" << seg.inGroup << "\n";
    }

    // Arc Segments
    out << "[NumArcSegments] = " << arcSegments.size() << "\n";
    for (const auto &arc : arcSegments) {
        int bpIdx = 0;
        if (arc.boundaryMarker != "<None>") {
            int idx = findBoundaryPropIndex(arc.boundaryMarker);
            if (idx >= 0) bpIdx = idx + 1;
        }
        out << arc.n0 << "\t" << arc.n1 << "\t"
            << arc.arcLength << "\t" << arc.maxSideLength << "\t"
            << bpIdx << "\t" << (arc.hidden ? 1 : 0) << "\t" << arc.inGroup << "\n";
    }

    // Holes + Block Labels
    // Count holes (blockType == "<No Mesh>")
    int numHoles = 0;
    for (const auto &blk : blockLabels)
        if (blk.blockType == "<No Mesh>") numHoles++;

    out << "[NumHoles] = " << numHoles << "\n";
    for (const auto &blk : blockLabels) {
        if (blk.blockType == "<No Mesh>") {
            out << blk.x << "\t" << blk.y << "\t" << blk.inGroup << "\n";
        }
    }

    out << "[NumBlockLabels] = " << ((int)blockLabels.size() - numHoles) << "\n";
    for (const auto &blk : blockLabels) {
        if (blk.blockType == "<No Mesh>") continue;

        int btIdx = 0;
        if (blk.blockType != "<None>") {
            int idx = findMaterialPropIndex(blk.blockType);
            if (idx >= 0) btIdx = idx + 1;
        }

        out << blk.x << "\t" << blk.y << "\t" << btIdx << "\t";

        // MaxArea: convert area back to diameter for file
        if (blk.maxArea > 0)
            out << std::sqrt(4.0 * blk.maxArea / M_PI);
        else
            out << "-1";

        // Circuit index
        int ciIdx = 0;
        if (blk.inCircuit != "<None>") {
            int idx = findCircuitPropIndex(blk.inCircuit);
            if (idx >= 0) ciIdx = idx + 1;
        }

        out << "\t" << ciIdx;
        out << "\t" << blk.magDir;
        out << "\t" << blk.inGroup;
        out << "\t" << blk.turns;

        int flags = (blk.isExternal ? 1 : 0) | (blk.isDefault ? 2 : 0)
                  | (blk.calculateLosses ? 4 : 0);
        out << "\t" << flags;

        // Reserved fields (were lamThickness/stackingFactor, now on material)
        out << "\t" << 0;
        out << "\t" << 0;

        // Write magDirFctn if set, or as empty "" placeholder if name follows
        if (!blk.magDirFctn.isEmpty() || !blk.name.isEmpty())
            out << "\t\"" << blk.magDirFctn << "\"";

        if (!blk.name.isEmpty())
            out << "\t\"" << blk.name << "\"";

        out << "\n";
    }

    file.close();
    m_filePath = path;
    isModified = false;
    return true;
}

// ---------------------------------------------------------------
// Geometry modification
// ---------------------------------------------------------------

bool FemmeDocument::addNode(double x, double y, double tol)
{
    // Check if a node already exists at this location
    for (const auto &nd : nodes) {
        if (nd.distanceTo(x, y) < tol) return false;
    }

    FNode nd;
    nd.x = x;
    nd.y = y;
    nodes.push_back(nd);
    isModified = true;
    return true;
}

bool FemmeDocument::addSegment(int n0, int n1)
{
    if (n0 == n1) return false;
    if (n0 < 0 || n0 >= (int)nodes.size()) return false;
    if (n1 < 0 || n1 >= (int)nodes.size()) return false;

    // Check for duplicate
    for (const auto &seg : segments) {
        if ((seg.n0 == n0 && seg.n1 == n1) ||
            (seg.n0 == n1 && seg.n1 == n0))
            return false;
    }

    FSegment seg;
    seg.n0 = n0;
    seg.n1 = n1;
    segments.push_back(seg);
    isModified = true;
    return true;
}

bool FemmeDocument::addArcSegment(int n0, int n1, double arcLen, double maxSeg)
{
    if (n0 == n1) return false;
    if (n0 < 0 || n0 >= (int)nodes.size()) return false;
    if (n1 < 0 || n1 >= (int)nodes.size()) return false;

    FArcSegment arc;
    arc.n0 = n0;
    arc.n1 = n1;
    arc.arcLength = arcLen;
    arc.maxSideLength = maxSeg;
    arcSegments.push_back(arc);
    isModified = true;
    return true;
}

bool FemmeDocument::addBlockLabel(double x, double y, double tol)
{
    // Check if a block label already exists nearby
    for (const auto &blk : blockLabels) {
        if (blk.distanceTo(x, y) < tol) return false;
    }

    FBlockLabel blk;
    blk.x = x;
    blk.y = y;
    blockLabels.push_back(blk);
    isModified = true;
    return true;
}

int FemmeDocument::closestNode(double x, double y) const
{
    int best = -1;
    double bestDist = 1e16;
    for (int i = 0; i < (int)nodes.size(); i++) {
        double d = nodes[i].distanceTo(x, y);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

int FemmeDocument::closestSegment(double x, double y) const
{
    // Simple distance to midpoint (could be improved with perpendicular distance)
    int best = -1;
    double bestDist = 1e16;
    for (int i = 0; i < (int)segments.size(); i++) {
        const FSegment &seg = segments[i];
        if (seg.n0 < 0 || seg.n0 >= (int)nodes.size()) continue;
        if (seg.n1 < 0 || seg.n1 >= (int)nodes.size()) continue;
        double mx = (nodes[seg.n0].x + nodes[seg.n1].x) / 2.0;
        double my = (nodes[seg.n0].y + nodes[seg.n1].y) / 2.0;
        double d = std::sqrt((x-mx)*(x-mx) + (y-my)*(y-my));
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

int FemmeDocument::closestBlockLabel(double x, double y) const
{
    int best = -1;
    double bestDist = 1e16;
    for (int i = 0; i < (int)blockLabels.size(); i++) {
        double d = blockLabels[i].distanceTo(x, y);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

bool FemmeDocument::getBoundingBox(double &xmin, double &ymin,
                                    double &xmax, double &ymax) const
{
    if (nodes.empty() && blockLabels.empty()) return false;

    xmin = ymin = 1e16;
    xmax = ymax = -1e16;

    for (const auto &nd : nodes) {
        xmin = std::min(xmin, nd.x);
        ymin = std::min(ymin, nd.y);
        xmax = std::max(xmax, nd.x);
        ymax = std::max(ymax, nd.y);
    }

    for (const auto &blk : blockLabels) {
        xmin = std::min(xmin, blk.x);
        ymin = std::min(ymin, blk.y);
        xmax = std::max(xmax, blk.x);
        ymax = std::max(ymax, blk.y);
    }

    // Add some padding
    double dx = xmax - xmin;
    double dy = ymax - ymin;
    if (dx < 1e-12 && dy < 1e-12) {
        // Single point — provide default range
        xmin -= 1.0; xmax += 1.0;
        ymin -= 1.0; ymax += 1.0;
    } else {
        double pad = std::max(dx, dy) * 0.05;
        xmin -= pad; xmax += pad;
        ymin -= pad; ymax += pad;
    }

    return true;
}

// ---------------------------------------------------------------
// Group selection and geometry transforms
// ---------------------------------------------------------------

void FemmeDocument::selectGroup(int group)
{
    for (auto &nd : nodes)
        if (nd.inGroup == group) nd.isSelected = true;
    for (auto &seg : segments)
        if (seg.inGroup == group) seg.isSelected = true;
    for (auto &arc : arcSegments)
        if (arc.inGroup == group) arc.isSelected = true;
    for (auto &blk : blockLabels)
        if (blk.inGroup == group) blk.isSelected = true;
}

void FemmeDocument::deselectAll()
{
    for (auto &nd : nodes) nd.isSelected = false;
    for (auto &seg : segments) seg.isSelected = false;
    for (auto &arc : arcSegments) arc.isSelected = false;
    for (auto &blk : blockLabels) blk.isSelected = false;
}

void FemmeDocument::translateSelected(double dx, double dy)
{
    for (auto &nd : nodes) {
        if (nd.isSelected) {
            nd.x += dx;
            nd.y += dy;
        }
    }
    for (auto &blk : blockLabels) {
        if (blk.isSelected) {
            blk.x += dx;
            blk.y += dy;
        }
    }
    isModified = true;
}

void FemmeDocument::rotateSelected(double cx, double cy, double angleDeg)
{
    double rad = angleDeg * M_PI / 180.0;
    double cosA = std::cos(rad);
    double sinA = std::sin(rad);

    for (auto &nd : nodes) {
        if (nd.isSelected) {
            double rx = nd.x - cx;
            double ry = nd.y - cy;
            nd.x = cx + rx * cosA - ry * sinA;
            nd.y = cy + rx * sinA + ry * cosA;
        }
    }
    for (auto &blk : blockLabels) {
        if (blk.isSelected) {
            double rx = blk.x - cx;
            double ry = blk.y - cy;
            blk.x = cx + rx * cosA - ry * sinA;
            blk.y = cy + rx * sinA + ry * cosA;
            // Rotate magnetization direction so magnets stay correctly oriented
            blk.magDir += angleDeg;
            // Normalize to [0, 360)
            blk.magDir = std::fmod(blk.magDir, 360.0);
            if (blk.magDir < 0) blk.magDir += 360.0;
        }
    }
    isModified = true;
}

// ---------------------------------------------------------------
// Delete selected items
// ---------------------------------------------------------------

int FemmeDocument::deleteSelectedNodes()
{
    // Build list of node indices to delete (in reverse order)
    std::vector<int> toDelete;
    for (int i = 0; i < (int)nodes.size(); i++) {
        if (nodes[i].isSelected) toDelete.push_back(i);
    }
    if (toDelete.empty()) return 0;

    // Delete from back to front so indices stay valid
    for (int k = (int)toDelete.size() - 1; k >= 0; k--) {
        int idx = toDelete[k];

        // Remove any segments or arcs that reference this node
        for (int i = (int)segments.size() - 1; i >= 0; i--) {
            if (segments[i].n0 == idx || segments[i].n1 == idx)
                segments.erase(segments.begin() + i);
        }
        for (int i = (int)arcSegments.size() - 1; i >= 0; i--) {
            if (arcSegments[i].n0 == idx || arcSegments[i].n1 == idx)
                arcSegments.erase(arcSegments.begin() + i);
        }

        // Remove the node
        nodes.erase(nodes.begin() + idx);

        // Update segment/arc node indices that were above the deleted index
        for (auto &seg : segments) {
            if (seg.n0 > idx) seg.n0--;
            if (seg.n1 > idx) seg.n1--;
        }
        for (auto &arc : arcSegments) {
            if (arc.n0 > idx) arc.n0--;
            if (arc.n1 > idx) arc.n1--;
        }
    }

    isModified = true;
    hasMesh = false;
    return (int)toDelete.size();
}

int FemmeDocument::deleteSelectedSegments()
{
    int count = 0;
    for (int i = (int)segments.size() - 1; i >= 0; i--) {
        if (segments[i].isSelected) {
            segments.erase(segments.begin() + i);
            count++;
        }
    }
    if (count > 0) { isModified = true; hasMesh = false; }
    return count;
}

int FemmeDocument::deleteSelectedArcSegments()
{
    int count = 0;
    for (int i = (int)arcSegments.size() - 1; i >= 0; i--) {
        if (arcSegments[i].isSelected) {
            arcSegments.erase(arcSegments.begin() + i);
            count++;
        }
    }
    if (count > 0) { isModified = true; hasMesh = false; }
    return count;
}

int FemmeDocument::deleteSelectedBlockLabels()
{
    int count = 0;
    for (int i = (int)blockLabels.size() - 1; i >= 0; i--) {
        if (blockLabels[i].isSelected) {
            blockLabels.erase(blockLabels.begin() + i);
            count++;
        }
    }
    if (count > 0) { isModified = true; hasMesh = false; }
    return count;
}

int FemmeDocument::deleteSelected()
{
    int count = 0;
    count += deleteSelectedSegments();
    count += deleteSelectedArcSegments();
    count += deleteSelectedBlockLabels();
    count += deleteSelectedNodes();  // nodes last — removes dependent segments/arcs too
    return count;
}

// ---------------------------------------------------------------
// Write mesh files (.node, .ele, .edge, .pbc) for standalone solver
// ---------------------------------------------------------------

#include "meshgen.h"  // for MeshEdge

bool FemmeDocument::writeMeshFiles(const QString &basePath,
                                    const std::vector<MeshEdge> &edges)
{
    // Write .node file
    {
        QFile f(basePath + ".node");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&f);
        int n = (int)meshNodes.size();
        out << n << "\t2\t0\t1\n";
        for (int i = 0; i < n; i++) {
            out << i << "\t" << meshNodes[i].x << "\t" << meshNodes[i].y
                << "\t" << meshNodes[i].boundaryMarker << "\n";
        }
    }

    // Write .ele file
    {
        QFile f(basePath + ".ele");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&f);
        int n = (int)meshElements.size();
        out << n << "\t3\t1\n";
        for (int i = 0; i < n; i++) {
            out << i << "\t" << meshElements[i].p[0] << "\t"
                << meshElements[i].p[1] << "\t" << meshElements[i].p[2]
                << "\t" << meshElements[i].label << "\n";
        }
    }

    // Write .edge file
    {
        QFile f(basePath + ".edge");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&f);
        int n = (int)edges.size();
        out << n << "\t1\n";
        for (int i = 0; i < n; i++) {
            out << i << "\t" << edges[i].n0 << "\t" << edges[i].n1
                << "\t" << edges[i].marker << "\n";
        }
    }

    // Write .pbc file (periodic boundary conditions — stub)
    {
        QFile f(basePath + ".pbc");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&f);
        out << "0\n0\n";
    }

    return true;
}
