// FEMM Qt 6 GUI — DXF R12 Import
// Reads DXF R12 ASCII files and adds geometry to FemmeDocument.
#ifndef DXFIMPORTER_H
#define DXFIMPORTER_H

#include <QString>
#include <QStringList>
#include <vector>

class FemmeDocument;
class QTextStream;

class DxfImporter
{
public:
    explicit DxfImporter(FemmeDocument *doc);

    // Two-phase API (allows UI to show tolerance dialog between phases):
    bool parseFile(const QString &filePath);     // Phase 1: parse DXF → temp lists
    double defaultTolerance() const;              // suggested merge tolerance
    void mergeIntoDocument(double tolerance);     // Phase 2: merge into document

    // Convenience one-shot (auto-tolerance):
    bool import(const QString &filePath, double tolerance = -1.0);

    QString errorMessage() const { return m_error; }
    int nodesAdded() const { return m_nodesAdded; }
    int segmentsAdded() const { return m_segmentsAdded; }
    int arcsAdded() const { return m_arcsAdded; }

private:
    // Temporary geometry (accumulated during parse, merged later)
    struct TempNode {
        double x = 0.0, y = 0.0;
        int inGroup = 0;
    };
    struct TempSegment {
        int n0 = 0, n1 = 0;
        int inGroup = 0;
    };
    struct TempArc {
        int n0 = 0, n1 = 0;
        double arcLength = 0.0;   // degrees
        int inGroup = 0;
    };

    // Helpers
    int addTempNode(double x, double y, int group);
    void addTempSegment(int n0, int n1, int group);
    void addTempArc(int n0, int n1, double arcLen, int group);

    // Layer → group mapping
    int layerToGroup(const QString &layerName);

    // Entity parsers (each reads group-code/value pairs until groupCode==0)
    void parseLayer(QTextStream &in);
    void parsePoint(QTextStream &in);
    void parseLine(QTextStream &in);
    void parseArc(QTextStream &in);
    void parseCircle(QTextStream &in);
    void parseLWPolyline(QTextStream &in);
    void parsePolyline(QTextStream &in);
    void parseVertex(QTextStream &in);
    void parseSeqEnd();

    // Arc helper: connect two nodes with arc, splitting if > 180°
    void addBulgeArc(int prevIdx, int curIdx, double angle, int group);

    // Skip entity (consume pairs until next entity boundary)
    void skipEntity(QTextStream &in);

    FemmeDocument *m_doc;
    QString m_error;
    int m_nodesAdded = 0;
    int m_segmentsAdded = 0;
    int m_arcsAdded = 0;

    // Temp geometry lists
    std::vector<TempNode> m_nodes;
    std::vector<TempSegment> m_segments;
    std::vector<TempArc> m_arcs;

    // Layer tracking
    QStringList m_layers;

    // Polyline state (carried between POLYLINE/VERTEX/SEQEND entities)
    bool m_polylineActive = false;
    bool m_polylineClosed = false;
    int m_polylineFirstNode = -1;
    int m_polylineLastNode = -1;
    double m_polylinePendingAngle = 0.0;
    int m_polylineGroup = 0;
    bool m_polylineFirstVertex = false;
};

#endif // DXFIMPORTER_H
