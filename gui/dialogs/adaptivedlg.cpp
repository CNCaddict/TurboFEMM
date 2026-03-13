// FEMM Qt 6 GUI — H-Adaptive Refinement Settings Dialog
#include "adaptivedlg.h"
#include "../adaptiverefine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSettings>

AdaptiveDialog::AdaptiveDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("H-Adaptive Mesh Refinement"));
    setMinimumWidth(360);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Description ---
    auto *descLabel = new QLabel(
        tr("Iteratively solves and refines the mesh, concentrating "
           "elements where the field error is highest. Then coarsens "
           "to find the minimum density mesh that meets the target."));
    descLabel->setWordWrap(true);
    descLabel->setToolTip(
        tr("H-Adaptive Mesh Refinement:\n\n"
           "Phase 1 — Refinement: Solves on the current mesh, estimates\n"
           "error using the Zienkiewicz-Zhu (ZZ) method, and refines\n"
           "regions where the error is highest.\n\n"
           "Phase 2 — Coarsening: After achieving the target, the algorithm\n"
           "coarsens the mesh to find the minimum element count that still\n"
           "meets the error tolerance. This prevents over-refinement.\n\n"
           "The result is the coarsest mesh that satisfies the ZZ error target."));
    mainLayout->addWidget(descLabel);

    // --- Precision slider ---
    auto *precBox = new QGroupBox(tr("Precision"));
    auto *precLayout = new QVBoxLayout(precBox);

    auto *sliderLayout = new QHBoxLayout;
    auto *lowLabel = new QLabel(tr("Coarse\n(faster)"));
    lowLabel->setAlignment(Qt::AlignLeft);
    auto *highLabel = new QLabel(tr("Fine\n(slower)"));
    highLabel->setAlignment(Qt::AlignRight);

    m_precisionSlider = new QSlider(Qt::Horizontal);
    m_precisionSlider->setRange(0, 100);
    m_precisionSlider->setTickPosition(QSlider::TicksBelow);
    m_precisionSlider->setTickInterval(25);
    m_precisionSlider->setToolTip(
        tr("Sets the Zienkiewicz-Zhu (ZZ) error tolerance target.\n\n"
           "The ZZ estimator measures mesh self-consistency by comparing\n"
           "the B field in each element with a smoothed average of its\n"
           "neighbors. It is NOT the direct simulation error.\n\n"
           "Rule of thumb: actual B-field accuracy is typically 2-10x\n"
           "better than the ZZ percentage. A 5% ZZ error usually means\n"
           "B-field results accurate to about 1-2%.\n\n"
           "The algorithm will coarsen or refine the mesh to approach\n"
           "this target, finding the minimum density mesh that meets it."));

    sliderLayout->addWidget(lowLabel);
    sliderLayout->addWidget(m_precisionSlider, 1);
    sliderLayout->addWidget(highLabel);
    precLayout->addLayout(sliderLayout);

    // --- Tolerance readout: label + editable spinbox ---
    auto *tolLayout = new QHBoxLayout;

    m_precisionLabel = new QLabel;
    tolLayout->addWidget(m_precisionLabel);

    tolLayout->addStretch();

    auto *tolLabel = new QLabel(tr("ZZ error target:"));
    tolLayout->addWidget(tolLabel);

    m_toleranceSpinBox = new QDoubleSpinBox;
    m_toleranceSpinBox->setRange(0.10, 20.00);
    m_toleranceSpinBox->setDecimals(2);
    m_toleranceSpinBox->setSuffix(tr(" %"));
    m_toleranceSpinBox->setSingleStep(0.25);
    m_toleranceSpinBox->setToolTip(
        tr("Type an exact ZZ error tolerance percentage.\n\n"
           "Range: 0.10% (very fine) to 20.00% (very coarse).\n"
           "The slider and this box stay in sync."));
    tolLayout->addWidget(m_toleranceSpinBox);

    precLayout->addLayout(tolLayout);

    mainLayout->addWidget(precBox);

    // --- Max iterations ---
    auto *iterBox = new QGroupBox(tr("Options"));
    auto *iterLayout = new QFormLayout(iterBox);

    m_maxIterations = new QSpinBox;
    m_maxIterations->setRange(1, 10);
    m_maxIterations->setToolTip(
        tr("Maximum number of solve-refine-coarsen passes.\n\n"
           "More iterations allow finer convergence toward the target\n"
           "error but take longer. 3-5 is usually sufficient."));
    iterLayout->addRow(tr("Max iterations:"), m_maxIterations);

    mainLayout->addWidget(iterBox);

    // --- Buttons ---
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AdaptiveDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // --- Restore saved settings ---
    QSettings settings;
    settings.beginGroup("AdaptiveRefine");
    m_precisionSlider->setValue(settings.value("precision", 50).toInt());
    m_maxIterations->setValue(settings.value("maxIterations", 5).toInt());
    settings.endGroup();

    // Connect slider and spinbox (bidirectional sync)
    connect(m_precisionSlider, &QSlider::valueChanged,
            this, &AdaptiveDialog::onSliderChanged);
    connect(m_toleranceSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &AdaptiveDialog::onToleranceChanged);
    onSliderChanged(m_precisionSlider->value());
}

// --- Slider ↔ tolerance mapping (log scale: 20% at 0, 0.1% at 100) ---
// tolerance = 0.20 * exp(-t * ln(200))   where t = slider / 100
// At slider=0:   0.20 * exp(0) = 0.20 (20%)
// At slider=100: 0.20 * exp(-ln(200)) = 0.001 (0.1%)

double AdaptiveDialog::sliderToTolerance(int value) const
{
    double t = value / 100.0;
    return 0.20 * std::exp(-t * std::log(200.0));
}

int AdaptiveDialog::toleranceToSlider(double tolerance) const
{
    // Inverse: t = ln(0.20 / tolerance) / ln(200)
    tolerance = std::clamp(tolerance, 0.001, 0.20);
    double t = std::log(0.20 / tolerance) / std::log(200.0);
    return std::clamp(static_cast<int>(std::round(t * 100.0)), 0, 100);
}

void AdaptiveDialog::onSliderChanged(int value)
{
    double tolerance = sliderToTolerance(value);

    QString level;
    if (value < 25)
        level = tr("Low");
    else if (value < 50)
        level = tr("Medium-Low");
    else if (value < 75)
        level = tr("Medium-High");
    else
        level = tr("High");

    m_precisionLabel->setText(level);

    // Update spinbox without re-entering onToleranceChanged
    m_toleranceSpinBox->blockSignals(true);
    m_toleranceSpinBox->setValue(tolerance * 100.0);
    m_toleranceSpinBox->blockSignals(false);
}

void AdaptiveDialog::onToleranceChanged(double percent)
{
    // User typed a tolerance — update slider to match
    double tolerance = percent / 100.0;
    int sliderVal = toleranceToSlider(tolerance);

    m_precisionSlider->blockSignals(true);
    m_precisionSlider->setValue(sliderVal);
    m_precisionSlider->blockSignals(false);

    // Update the level label
    QString level;
    if (sliderVal < 25)
        level = tr("Low");
    else if (sliderVal < 50)
        level = tr("Medium-Low");
    else if (sliderVal < 75)
        level = tr("Medium-High");
    else
        level = tr("High");

    m_precisionLabel->setText(level);
}

AdaptiveConfig AdaptiveDialog::config() const
{
    AdaptiveConfig cfg;

    // Read directly from spinbox for maximum precision
    cfg.tolerance = m_toleranceSpinBox->value() / 100.0;
    cfg.maxIterations = m_maxIterations->value();
    cfg.markingFraction = 0.20;

    return cfg;
}

void AdaptiveDialog::onAccept()
{
    // Save settings for next time
    QSettings settings;
    settings.beginGroup("AdaptiveRefine");
    settings.setValue("precision", m_precisionSlider->value());
    settings.setValue("maxIterations", m_maxIterations->value());
    settings.endGroup();

    accept();
}
