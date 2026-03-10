// FEMM Qt 6 GUI — Add Boundary Region Dialog
#include "boundaryregiondlg.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QSettings>

BoundaryRegionDialog::BoundaryRegionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Boundary Region"));
    setMinimumWidth(340);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Description ---
    auto *descLabel = new QLabel(
        tr("Creates a surrounding air region with an A=0 boundary\n"
           "condition, providing a flux return path for the model.\n"
           "This region is excluded from H-adaptive refinement."));
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // --- Form ---
    auto *formLayout = new QFormLayout;

    m_shape = new QComboBox;
    m_shape->addItem(tr("Circle"));
    m_shape->addItem(tr("Rectangle"));
    m_shape->setToolTip(tr("Shape of the bounding region.\n\n"
        "Circle: two 180-degree arcs forming a full circle.\n"
        "Rectangle: four straight line segments."));
    formLayout->addRow(tr("Shape:"), m_shape);

    m_distance = new QDoubleSpinBox;
    m_distance->setRange(0.1, 100000.0);
    m_distance->setDecimals(2);
    m_distance->setSuffix(tr(" mm"));
    m_distance->setToolTip(tr("Distance from the model bounding box edge\n"
        "to the boundary region. Larger values give more\n"
        "room for the flux return path."));
    formLayout->addRow(tr("Distance:"), m_distance);

    m_meshSize = new QDoubleSpinBox;
    m_meshSize->setRange(0.0, 100000.0);
    m_meshSize->setDecimals(2);
    m_meshSize->setSuffix(tr(" mm"));
    m_meshSize->setSpecialValueText(tr("Auto"));
    m_meshSize->setToolTip(tr("Maximum triangle area in the boundary region.\n\n"
        "Set to 0 (Auto) for no constraint — the mesher\n"
        "will choose element sizes automatically."));
    formLayout->addRow(tr("Mesh size:"), m_meshSize);

    mainLayout->addLayout(formLayout);

    // --- Buttons ---
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &BoundaryRegionDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // --- Restore saved settings ---
    QSettings settings;
    settings.beginGroup("BoundaryRegion");
    m_shape->setCurrentIndex(settings.value("shape", 0).toInt());
    m_distance->setValue(settings.value("distance", 10.0).toDouble());
    m_meshSize->setValue(settings.value("meshSize", 0.0).toDouble());
    settings.endGroup();
}

BoundaryRegionConfig BoundaryRegionDialog::config() const
{
    BoundaryRegionConfig cfg;
    cfg.shape = (m_shape->currentIndex() == 0) ? BoundaryRegionConfig::Circle
                                                : BoundaryRegionConfig::Rectangle;
    cfg.distance = m_distance->value();
    cfg.meshSize = m_meshSize->value();
    return cfg;
}

void BoundaryRegionDialog::onAccept()
{
    // Save settings for next time
    QSettings settings;
    settings.beginGroup("BoundaryRegion");
    settings.setValue("shape", m_shape->currentIndex());
    settings.setValue("distance", m_distance->value());
    settings.setValue("meshSize", m_meshSize->value());
    settings.endGroup();

    accept();
}
