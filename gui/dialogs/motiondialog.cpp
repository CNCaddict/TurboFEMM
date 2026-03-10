// FEMM Qt 6 GUI — Parametric Motion Dialog
#include "motiondialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QSettings>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MotionDialog::MotionDialog(const QStringList &circuitNames,
                           OptimizeCallback optimizeCallback,
                           QWidget *parent)
    : QDialog(parent)
    , m_optimizeCallback(std::move(optimizeCallback))
{
    setWindowTitle(tr("Parametric Motion"));
    setMinimumWidth(420);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Group selection ---
    auto *groupBox = new QGroupBox(tr("Group"));
    auto *groupLayout = new QFormLayout(groupBox);

    m_groupNumber = new QSpinBox;
    m_groupNumber->setRange(0, 99);
    m_groupNumber->setValue(1);
    groupLayout->addRow(tr("Group number:"), m_groupNumber);

    mainLayout->addWidget(groupBox);

    // --- Motion type ---
    auto *motionBox = new QGroupBox(tr("Motion"));
    auto *motionLayout = new QVBoxLayout(motionBox);

    auto *typeLayout = new QFormLayout;
    m_motionType = new QComboBox;
    m_motionType->addItem(tr("Translate"));
    m_motionType->addItem(tr("Rotate"));
    typeLayout->addRow(tr("Type:"), m_motionType);
    motionLayout->addLayout(typeLayout);

    // Stacked widget for translate/rotate params
    m_paramStack = new QStackedWidget;

    // Page 0: Translate
    auto *translatePage = new QWidget;
    auto *tLayout = new QFormLayout(translatePage);
    m_totalDistance = new QDoubleSpinBox;
    m_totalDistance->setRange(0.0, 1e6);
    m_totalDistance->setDecimals(6);
    m_totalDistance->setValue(1.0);
    m_totalDistance->setSuffix(tr(" mm"));
    tLayout->addRow(tr("Total distance:"), m_totalDistance);

    m_directionAngle = new QDoubleSpinBox;
    m_directionAngle->setRange(-360.0, 360.0);
    m_directionAngle->setDecimals(2);
    m_directionAngle->setValue(0.0);
    m_directionAngle->setSuffix(tr(" deg"));
    tLayout->addRow(tr("Direction angle:"), m_directionAngle);

    m_paramStack->addWidget(translatePage);

    // Page 1: Rotate
    auto *rotatePage = new QWidget;
    auto *rLayout = new QFormLayout(rotatePage);
    m_cx = new QDoubleSpinBox;
    m_cx->setRange(-1e6, 1e6);
    m_cx->setDecimals(6);
    m_cx->setValue(0.0);
    rLayout->addRow(tr("Center X:"), m_cx);

    m_cy = new QDoubleSpinBox;
    m_cy->setRange(-1e6, 1e6);
    m_cy->setDecimals(6);
    m_cy->setValue(0.0);
    rLayout->addRow(tr("Center Y:"), m_cy);

    m_angle = new QDoubleSpinBox;
    m_angle->setRange(-36000.0, 36000.0);
    m_angle->setDecimals(2);
    m_angle->setValue(360.0);
    m_angle->setSuffix(tr(" deg"));
    rLayout->addRow(tr("Total angle:"), m_angle);

    m_paramStack->addWidget(rotatePage);

    motionLayout->addWidget(m_paramStack);
    mainLayout->addWidget(motionBox);

    connect(m_motionType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MotionDialog::onMotionTypeChanged);

    // --- Steps ---
    auto *stepsBox = new QGroupBox(tr("Sweep"));
    auto *stepsLayout = new QFormLayout(stepsBox);

    m_numSteps = new QSpinBox;
    m_numSteps->setRange(1, 10000);
    m_numSteps->setValue(10);
    stepsLayout->addRow(tr("Number of steps:"), m_numSteps);

    mainLayout->addWidget(stepsBox);

    // --- Motor Module ---
    m_motorEnabled = new QCheckBox(tr("Enable Motor Module"));
    m_motorEnabled->setChecked(false);
    mainLayout->addWidget(m_motorEnabled);

    m_motorGroup = new QGroupBox(tr("3-Phase Motor"));
    m_motorGroup->setEnabled(false);
    auto *motorLayout = new QFormLayout(m_motorGroup);

    m_motorRmsCurrent = new QDoubleSpinBox;
    m_motorRmsCurrent->setRange(0.0, 1e6);
    m_motorRmsCurrent->setDecimals(3);
    m_motorRmsCurrent->setSuffix(tr(" A"));
    m_motorRmsCurrent->setValue(1.0);
    motorLayout->addRow(tr("RMS Current:"), m_motorRmsCurrent);

    m_motorInitialAngle = new QDoubleSpinBox;
    m_motorInitialAngle->setRange(-360.0, 360.0);
    m_motorInitialAngle->setDecimals(2);
    m_motorInitialAngle->setSuffix(tr(" deg"));
    m_motorInitialAngle->setValue(0.0);
    motorLayout->addRow(tr("Initial Phase Angle:"), m_motorInitialAngle);

    // Phase assignment combo boxes
    m_motorPhaseA = new QComboBox;
    m_motorPhaseB = new QComboBox;
    m_motorPhaseC = new QComboBox;
    m_motorPhaseA->addItem(tr("<None>"));
    m_motorPhaseB->addItem(tr("<None>"));
    m_motorPhaseC->addItem(tr("<None>"));
    for (const auto &name : circuitNames) {
        m_motorPhaseA->addItem(name);
        m_motorPhaseB->addItem(name);
        m_motorPhaseC->addItem(name);
    }
    motorLayout->addRow(tr("Phase A Circuit:"), m_motorPhaseA);
    motorLayout->addRow(tr("Phase B Circuit:"), m_motorPhaseB);
    motorLayout->addRow(tr("Phase C Circuit:"), m_motorPhaseC);

    m_motorPolePairs = new QSpinBox;
    m_motorPolePairs->setRange(1, 100);
    m_motorPolePairs->setValue(1);
    motorLayout->addRow(tr("Pole Pairs (rotation):"), m_motorPolePairs);

    m_motorPolePitch = new QDoubleSpinBox;
    m_motorPolePitch->setRange(0.001, 1e6);
    m_motorPolePitch->setDecimals(4);
    m_motorPolePitch->setValue(10.0);
    m_motorPolePitch->setSuffix(tr(" mm"));
    motorLayout->addRow(tr("Pole Pitch (translation):"), m_motorPolePitch);

    m_motorAngleStep = new QDoubleSpinBox;
    m_motorAngleStep->setRange(0.1, 30.0);
    m_motorAngleStep->setDecimals(1);
    m_motorAngleStep->setSuffix(tr(" deg"));
    m_motorAngleStep->setValue(1.0);
    motorLayout->addRow(tr("Optimisation Step:"), m_motorAngleStep);

    // Reverse phase direction
    m_motorReversePhase = new QCheckBox(tr("Reverse phase direction"));
    m_motorReversePhase->setChecked(false);
    motorLayout->addRow(QString(), m_motorReversePhase);

    // Optimise row: button + read-only result
    auto *optLayout = new QHBoxLayout;
    m_motorOptimiseBtn = new QPushButton(tr("Optimise Angle"));
    connect(m_motorOptimiseBtn, &QPushButton::clicked,
            this, &MotionDialog::onMotorOptimise);
    optLayout->addWidget(m_motorOptimiseBtn);

    m_motorOptimalAngle = new QLineEdit;
    m_motorOptimalAngle->setReadOnly(true);
    m_motorOptimalAngle->setPlaceholderText(tr("Not optimised"));
    optLayout->addWidget(m_motorOptimalAngle);
    motorLayout->addRow(tr("Optimal Angle:"), optLayout);

    mainLayout->addWidget(m_motorGroup);

    connect(m_motorEnabled, &QCheckBox::toggled,
            this, &MotionDialog::onMotorEnabledChanged);

    // --- Output ---
    auto *outputBox = new QGroupBox(tr("Output"));
    auto *outputVLayout = new QVBoxLayout(outputBox);

    auto *dirLayout = new QHBoxLayout;
    m_outputDir = new QLineEdit;
    m_outputDir->setPlaceholderText(tr("Select output directory..."));
    dirLayout->addWidget(m_outputDir);

    auto *browseBtn = new QPushButton(tr("Browse..."));
    connect(browseBtn, &QPushButton::clicked, this, &MotionDialog::onBrowseOutput);
    dirLayout->addWidget(browseBtn);
    outputVLayout->addLayout(dirLayout);

    m_saveImages = new QCheckBox(tr("Save PNG frames"));
    m_saveImages->setChecked(true);
    outputVLayout->addWidget(m_saveImages);

    m_saveVideo = new QCheckBox(tr("Save animated GIF"));
    m_saveVideo->setChecked(true);
    outputVLayout->addWidget(m_saveVideo);

    m_loopPlayback = new QCheckBox(tr("Loop playback (skip last frame)"));
    m_loopPlayback->setChecked(false);
    outputVLayout->addWidget(m_loopPlayback);

    // CSV row: checkbox + options button
    auto *csvLayout = new QHBoxLayout;
    m_saveCSV = new QCheckBox(tr("Save CSV results"));
    m_saveCSV->setChecked(true);
    csvLayout->addWidget(m_saveCSV);

    auto *csvOptsBtn = new QPushButton(tr("CSV Options..."));
    csvOptsBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(csvOptsBtn, &QPushButton::clicked, this, &MotionDialog::onCSVOptions);
    csvLayout->addWidget(csvOptsBtn);
    csvLayout->addStretch();
    outputVLayout->addLayout(csvLayout);

    mainLayout->addWidget(outputBox);

    // --- Buttons ---
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    auto *runBtn = new QPushButton(tr("Run"));
    runBtn->setDefault(true);
    connect(runBtn, &QPushButton::clicked, this, &MotionDialog::onAccept);
    btnLayout->addWidget(runBtn);

    auto *cancelBtn = new QPushButton(tr("Cancel"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);

    // --- Restore saved settings ---
    QSettings settings;
    settings.beginGroup("motion");
    m_groupNumber->setValue(settings.value("groupNumber", 1).toInt());
    m_motionType->setCurrentIndex(settings.value("motionType", 0).toInt());
    m_totalDistance->setValue(settings.value("totalDistance", 1.0).toDouble());
    m_directionAngle->setValue(settings.value("directionAngle", 0.0).toDouble());
    m_cx->setValue(settings.value("cx", 0.0).toDouble());
    m_cy->setValue(settings.value("cy", 0.0).toDouble());
    m_angle->setValue(settings.value("angle", 5.0).toDouble());
    m_numSteps->setValue(settings.value("numSteps", 10).toInt());
    m_outputDir->setText(settings.value("outputDir", QString()).toString());
    m_saveImages->setChecked(settings.value("saveImages", true).toBool());
    m_saveVideo->setChecked(settings.value("saveVideo", true).toBool());
    m_loopPlayback->setChecked(settings.value("loopPlayback", false).toBool());
    m_saveCSV->setChecked(settings.value("saveCSV", true).toBool());
    m_csvFluxDensity = settings.value("csvFluxDensity", true).toBool();
    m_csvVectorPotential = settings.value("csvVectorPotential", true).toBool();
    m_csvEnergy = settings.value("csvEnergy", true).toBool();
    m_csvForceTorque = settings.value("csvForceTorque", true).toBool();

    // Motor settings
    m_motorEnabled->setChecked(settings.value("motorEnabled", false).toBool());
    m_motorRmsCurrent->setValue(settings.value("motorRmsCurrent", 1.0).toDouble());
    m_motorInitialAngle->setValue(settings.value("motorInitialAngle", 0.0).toDouble());
    // Phase circuit selection: try to restore by name
    QString savedA = settings.value("motorPhaseA", "<None>").toString();
    QString savedB = settings.value("motorPhaseB", "<None>").toString();
    QString savedC = settings.value("motorPhaseC", "<None>").toString();
    int idxA = m_motorPhaseA->findText(savedA);
    int idxB = m_motorPhaseB->findText(savedB);
    int idxC = m_motorPhaseC->findText(savedC);
    if (idxA >= 0) m_motorPhaseA->setCurrentIndex(idxA);
    if (idxB >= 0) m_motorPhaseB->setCurrentIndex(idxB);
    if (idxC >= 0) m_motorPhaseC->setCurrentIndex(idxC);
    m_motorPolePairs->setValue(settings.value("motorPolePairs", 1).toInt());
    m_motorPolePitch->setValue(settings.value("motorPolePitch", 10.0).toDouble());
    m_motorAngleStep->setValue(settings.value("motorAngleStep", 1.0).toDouble());
    m_motorReversePhase->setChecked(settings.value("motorReversePhase", false).toBool());
    // Restore optimisation result
    m_motorOptimized = settings.value("motorOptimized", false).toBool();
    m_motorOptimalAngleValue = settings.value("motorOptimalAngle", 0.0).toDouble();
    if (m_motorOptimized) {
        m_motorOptimalAngle->setText(
            QString::number(m_motorOptimalAngleValue, 'f', 2) + tr(" deg"));
    }
    settings.endGroup();

    // Sync stacked widget to restored motion type
    m_paramStack->setCurrentIndex(m_motionType->currentIndex());
    // Sync motor group enabled state
    m_motorGroup->setEnabled(m_motorEnabled->isChecked());

    // Save settings on every close path (Run, Cancel, window close button)
    connect(this, &QDialog::finished, this, &MotionDialog::saveSettings);
}

void MotionDialog::onMotionTypeChanged(int index)
{
    m_paramStack->setCurrentIndex(index);
}

void MotionDialog::onMotorEnabledChanged(bool checked)
{
    m_motorGroup->setEnabled(checked);
}

void MotionDialog::onMotorOptimise()
{
    if (!m_optimizeCallback) {
        QMessageBox::warning(this, tr("Error"),
            tr("Optimisation not available — no callback configured."));
        return;
    }

    // Validate phase assignments
    QString phaseA = m_motorPhaseA->currentText();
    QString phaseB = m_motorPhaseB->currentText();
    QString phaseC = m_motorPhaseC->currentText();
    if (phaseA == tr("<None>") || phaseB == tr("<None>") || phaseC == tr("<None>")) {
        QMessageBox::warning(this, tr("Missing Phase"),
            tr("Please assign all three phases to circuits before optimising."));
        return;
    }

    // Disable button during optimisation
    m_motorOptimiseBtn->setEnabled(false);
    m_motorOptimiseBtn->setText(tr("Optimising..."));

    double optAngle = 0.0;
    QString error;

    bool ok = m_optimizeCallback(
        m_motorRmsCurrent->value(),
        m_motorInitialAngle->value(),
        m_motorAngleStep->value(),
        m_motorPolePairs->value(),
        phaseA, phaseB, phaseC,
        optAngle, error
    );

    m_motorOptimiseBtn->setEnabled(true);
    m_motorOptimiseBtn->setText(tr("Optimise Angle"));

    if (ok) {
        m_motorOptimalAngleValue = optAngle;
        m_motorOptimized = true;
        m_motorOptimalAngle->setText(
            QString::number(optAngle, 'f', 2) + tr(" deg"));
        // Persist immediately so the result survives Cancel / close
        saveSettings();
    } else {
        QMessageBox::warning(this, tr("Optimisation Failed"), error);
    }
}

void MotionDialog::onBrowseOutput()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Output Directory"),
        m_outputDir->text().isEmpty() ? QDir::homePath() : m_outputDir->text());
    if (!dir.isEmpty())
        m_outputDir->setText(dir);
}

void MotionDialog::onAccept()
{
    if (m_outputDir->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Output"),
            tr("Please select an output directory."));
        return;
    }

    QDir dir(m_outputDir->text());
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            QMessageBox::warning(this, tr("Error"),
                tr("Could not create output directory."));
            return;
        }
    }

    saveSettings();
    accept();
}

void MotionDialog::onCSVOptions()
{
    // Build a small popup dialog with checkboxes for CSV column categories
    QDialog dlg(this);
    dlg.setWindowTitle(tr("CSV Options"));

    auto *layout = new QVBoxLayout(&dlg);

    auto *label = new QLabel(tr("Select data columns to include in CSV:"));
    layout->addWidget(label);

    auto *chkFlux = new QCheckBox(tr("Flux density (B_max, B_min, B_avg)"));
    chkFlux->setChecked(m_csvFluxDensity);
    layout->addWidget(chkFlux);

    auto *chkPotential = new QCheckBox(tr("Vector potential (A_max, A_min)"));
    chkPotential->setChecked(m_csvVectorPotential);
    layout->addWidget(chkPotential);

    auto *chkEnergy = new QCheckBox(tr("Energy && area"));
    chkEnergy->setChecked(m_csvEnergy);
    layout->addWidget(chkEnergy);

    auto *chkForce = new QCheckBox(tr("Force / torque"));
    chkForce->setChecked(m_csvForceTorque);
    layout->addWidget(chkForce);

    auto *note = new QLabel(tr("Position columns (Step, Displacement, Angle) are always included."));
    note->setWordWrap(true);
    note->setStyleSheet("color: gray; font-size: 10px;");
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        m_csvFluxDensity = chkFlux->isChecked();
        m_csvVectorPotential = chkPotential->isChecked();
        m_csvEnergy = chkEnergy->isChecked();
        m_csvForceTorque = chkForce->isChecked();
    }
}

MotionConfig MotionDialog::config() const
{
    MotionConfig c;
    c.groupNumber = m_groupNumber->value();
    c.isRotation = (m_motionType->currentIndex() == 1);
    c.numSteps = m_numSteps->value();

    // Translation: compute per-step dx/dy from total distance + direction
    c.totalDistance = m_totalDistance->value();
    c.directionAngle = m_directionAngle->value();
    {
        double stepDist = c.totalDistance / (double)c.numSteps;
        double rad = c.directionAngle * M_PI / 180.0;
        c.dx = stepDist * std::cos(rad);
        c.dy = stepDist * std::sin(rad);
    }

    // Rotation: user enters total angle; compute per-step angle for the runner
    c.cx = m_cx->value();
    c.cy = m_cy->value();
    c.angle = m_angle->value() / (double)c.numSteps;
    c.outputDir = m_outputDir->text();
    c.saveImages = m_saveImages->isChecked();
    c.saveVideo = m_saveVideo->isChecked();
    c.loopPlayback = m_loopPlayback->isChecked();
    c.saveCSV = m_saveCSV->isChecked();
    c.csvFluxDensity = m_csvFluxDensity;
    c.csvVectorPotential = m_csvVectorPotential;
    c.csvEnergy = m_csvEnergy;
    c.csvForceTorque = m_csvForceTorque;

    // Motor module
    c.motorEnabled = m_motorEnabled->isChecked();
    c.motorRmsCurrent = m_motorRmsCurrent->value();
    c.motorInitialAngle = m_motorInitialAngle->value();
    c.motorPhaseA = m_motorPhaseA->currentText();
    c.motorPhaseB = m_motorPhaseB->currentText();
    c.motorPhaseC = m_motorPhaseC->currentText();
    c.motorPolePairs = m_motorPolePairs->value();
    c.motorPolePitch = m_motorPolePitch->value();
    c.motorAngleStep = m_motorAngleStep->value();
    c.motorOptimalAngle = m_motorOptimalAngleValue;
    c.motorOptimized = m_motorOptimized;
    c.motorReversePhase = m_motorReversePhase->isChecked();
    return c;
}

void MotionDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup("motion");
    settings.setValue("groupNumber", m_groupNumber->value());
    settings.setValue("motionType", m_motionType->currentIndex());
    settings.setValue("totalDistance", m_totalDistance->value());
    settings.setValue("directionAngle", m_directionAngle->value());
    settings.setValue("cx", m_cx->value());
    settings.setValue("cy", m_cy->value());
    settings.setValue("angle", m_angle->value());
    settings.setValue("numSteps", m_numSteps->value());
    settings.setValue("outputDir", m_outputDir->text());
    settings.setValue("saveImages", m_saveImages->isChecked());
    settings.setValue("saveVideo", m_saveVideo->isChecked());
    settings.setValue("loopPlayback", m_loopPlayback->isChecked());
    settings.setValue("saveCSV", m_saveCSV->isChecked());
    settings.setValue("csvFluxDensity", m_csvFluxDensity);
    settings.setValue("csvVectorPotential", m_csvVectorPotential);
    settings.setValue("csvEnergy", m_csvEnergy);
    settings.setValue("csvForceTorque", m_csvForceTorque);
    // Motor settings
    settings.setValue("motorEnabled", m_motorEnabled->isChecked());
    settings.setValue("motorRmsCurrent", m_motorRmsCurrent->value());
    settings.setValue("motorInitialAngle", m_motorInitialAngle->value());
    settings.setValue("motorPhaseA", m_motorPhaseA->currentText());
    settings.setValue("motorPhaseB", m_motorPhaseB->currentText());
    settings.setValue("motorPhaseC", m_motorPhaseC->currentText());
    settings.setValue("motorPolePairs", m_motorPolePairs->value());
    settings.setValue("motorPolePitch", m_motorPolePitch->value());
    settings.setValue("motorAngleStep", m_motorAngleStep->value());
    settings.setValue("motorReversePhase", m_motorReversePhase->isChecked());
    settings.setValue("motorOptimized", m_motorOptimized);
    settings.setValue("motorOptimalAngle", m_motorOptimalAngleValue);
    settings.endGroup();
}
