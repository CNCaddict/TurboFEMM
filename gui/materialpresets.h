// =============================================================
// Steinmetz iron loss presets for common electrical steels
//
// Loss model: P = Kh*f*B^alpha + Kc*(f*B)^2 + Ke*(f*B)^1.5  [W/m^3]
//
// Kc computed analytically: Kc = sigma * pi^2 * d^2 / 6
//   where d = nominal lamination thickness, sigma = conductivity
// Kh fitted from ASTM A677 guaranteed max core loss at 1.5T/60Hz
// Ke estimated as ~10% of total loss (typical for silicon steels)
// alpha = 2.0 standard for silicon steel above 1T
//
// These are conservative estimates from public datasheet values.
// Users should refine using manufacturer-specific loss curves.
// =============================================================
#ifndef MATERIALPRESETS_H
#define MATERIALPRESETS_H

#include <QString>
#include <vector>

struct MaterialPreset {
    QString name;
    double Kh;          // hysteresis coefficient [W*s/(T^alpha * m^3)]
    double Kc;          // eddy current coefficient [W*s^2/(T^2 * m^3)]
    double Ke;          // excess loss coefficient [W*s^1.5/(T^1.5 * m^3)]
    double alpha;       // Steinmetz exponent
    double sigma;       // conductivity (MS/m)
    double density;     // material density (kg/m^3)
    double nomThick;    // nominal lamination thickness (mm)
};

// Preset library — values derived from ASTM A677 and published literature
// Kc values assume nominal lamination thickness listed
inline const std::vector<MaterialPreset> &materialPresets()
{
    static const std::vector<MaterialPreset> presets = {
        //  name             Kh      Kc      Ke     alpha  sigma  density  nomThick
        { "M-15 (29ga)",    157.0,  0.569,  1.37,  2.0,   2.84,  7650,   0.36 },
        { "M-19 (29ga)",    179.0,  0.569,  1.56,  2.0,   2.67,  7700,   0.36 },
        { "M-19 (26ga)",    179.0,  0.994,  1.56,  2.0,   2.67,  7700,   0.47 },
        { "M-19 (24ga)",    179.0,  1.837,  1.56,  2.0,   2.67,  7700,   0.64 },
        { "M-22 (29ga)",    193.0,  0.569,  1.68,  2.0,   2.56,  7700,   0.36 },
        { "M-27 (29ga)",    215.0,  0.569,  1.87,  2.0,   2.45,  7700,   0.36 },
        { "M-36 (29ga)",    249.0,  0.476,  2.17,  2.0,   2.33,  7700,   0.36 },
        { "M-43 (29ga)",    283.0,  0.404,  2.46,  2.0,   2.12,  7750,   0.36 },
        { "M-47 (24ga)",    318.0,  0.355,  2.77,  2.0,   1.92,  7800,   0.64 },
        { "Hiperco 50",      50.0,  0.827,  0.80,  1.8,   2.50,  8120,   0.36 },
        { "Pure Iron",      350.0,  1.646,  3.00,  2.0,  10.00,  7874,   0.36 },
    };
    return presets;
}

#endif // MATERIALPRESETS_H
