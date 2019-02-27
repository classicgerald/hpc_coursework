#ifndef CLASS_BURGERS2P
#define CLASS_BURGERS2P

#include "Model2P.h"
#include <fstream>

/**
 * @class Burgers2P
 * @brief Creates a Burgers instance that does computations on Burger's equation
 * */
class Burgers2P {
public:
    Burgers2P(Model &m);
    ~Burgers2P();

    void SetInitialVelocity();
    void SetIntegratedVelocity();
    void WriteVelocityFile();
    void SetEnergy();
private:
    void WriteOf(double** Vel, double** M, std::ofstream &of, char id);
    double ComputeR(double x, double y);
    double CalculateEnergyState(double* Ui, double* Vi);
    double* NextVelocityState(double* Ui, double* Vi, bool U_OR_V);
    void SetMatrixCoefficients();
    void SetCaches(double* Vel);
    void AssembleMatrix(double* Vel, double** M);
    void UpdateBoundsLinear(double* dVel_dx_2, double* dVel_dy_2, double* dVel_dx, double* dVel_dy);
    void UpdateBoundsNonLinear(double* Vel, double* Other, double* Vel_Vel_Minus_1, double* Vel_Other_Minus_1, bool SELECT_U);

    /// Burger parameters
    Model* model;
    double** U;
    double** V;
    double* U0;
    double* E;
    double* dVel_dx_2_coeffs;
    double* dVel_dy_2_coeffs;
    double* dVel_dx_coeffs;
    double* dVel_dy_coeffs;

    /// Caches for partitioning matrix
    double* upVel;
    double* downVel;
    double* leftVel;
    double* rightVel;
};
#endif //CLASS_BURGERS2P
