#include <cmath>
#include <fstream>
#include <iomanip>
#include "BLAS_Wrapper.h"
#include "Helpers.h"
#include "Burgers.h"
#include <iostream>
using namespace std;

/**
 * @brief Public Constructor: Accepts a Model instance reference as input
 * @param &m reference to Model instance
 * */
Burgers::Burgers(Model &m) {
    model = &m;

    /// Get model parameters
    int Ny = model->GetNy();
    int Nx = model->GetNx();

    /// Reduced parameters
    int Nyr = Ny - 2;
    int Nxr = Nx - 2;

    /// Allocate memory to instance variables
    local.U = new double[Nyr*Nxr];
    local.V = new double[Nyr*Nxr];
    local.NextU = new double[Nyr*Nxr];
    local.NextV = new double[Nyr*Nxr];
}

/**
 * @brief Destructor: Deletes all allocated pointers in the class instance
 * */
Burgers::~Burgers() {
    /// Delete U and V
    delete[] local.U;
    delete[] local.V;
    delete[] local.NextU;
    delete[] local.NextV;

    /// model is not dynamically alloc
}

/**
 * @brief Sets initial velocity field in x,y for U0 (V0 = U0)
 * */
void Burgers::SetInitialVelocity() {
    /// Get model parameters
    int Ny = model->GetNy();
    int Nx = model->GetNx();
    double x0 = model->GetX0();
    double y0 = model->GetY0();
    double dx = model->GetDx();
    double dy = model->GetDy();

    /// Reduced parameters
    int Nyr = Ny - 2;
    int Nxr = Nx - 2;

    /// Compute U0;
    for (int i = 0; i < Nxr; i++) {
        for (int j = 0; j < Nyr; j++) {
            // Assumes x0 and y0 are identifying top LHS of matrix
            double y = y0 - (j+1)*dy;
            double x = x0 + (i+1)*dx;
            double r = pow(x*x+y*y, 0.5);
            // Store in column-major format
            local.U[i*Nyr+j] = (r <= 1.0)? 2.0*pow(1.0-r,4.0) * (4.0*r+1.0) : 0.0;
            local.V[i*Nyr+j] = (r <= 1.0)? 2.0*pow(1.0-r,4.0) * (4.0*r+1.0) : 0.0;
        }
    }
}

/**
 * @brief Sets velocity field in x,y for U, V
 * */
void Burgers::SetIntegratedVelocity() {
    /// Get model parameters
    int Ny = model->GetNy();
    int Nx = model->GetNx();
    int Nt = model->GetNt();

    /// Reduced parameters
    int Nyr = Ny - 2;
    int Nxr = Nx - 2;

    /// Compute U, V for every step k
    for (int k = 0; k < Nt-1; k++) {
        NextVelocityState(local.NextU, true);
        NextVelocityState(local.NextV, false);
        /// Delete current pointer and point to NextVel
        F77NAME(dcopy)(Nyr*Nxr, local.NextU, 1, local.U, 1);
        F77NAME(dcopy)(Nyr*Nxr, local.NextV, 1, local.V, 1);
        cout << "step: " << k << "\n";
    }
}

/**
 * @brief Writes the velocity field for U, V into a file
 * IMPORTANT: Run SetIntegratedVelocity() first
 * */
void Burgers::WriteVelocityFile() {
    /// Get model parameters
    int Ny = model->GetNy();
    int Nx = model->GetNx();

    /// Reduced parameters
    int Nyr = Ny - 2;
    int Nxr = Nx - 2;

    /// Alloc 2D pointer
    double** Vel = new double*[Nyr];
    for (int j = 0; j < Nyr; j++) {
        Vel[j] = new double[Nxr];
    }

    /// Write U, V into "data.txt"
    ofstream of;
    of.open("data.txt", ios::out | ios::trunc);
    of.precision(4); // 4 s.f.
    /// Write U velocities
    of << "U velocity field:" << endl;
    wrap(local.U, Nyr, Nxr, Vel);
    for (int j = 0; j < Ny; j++) {
        for (int i = 0; i < Nx; i++) {
            if (j == 0 || i == 0 || j == Ny-1 || i == Nx-1) {
                of << 0 << ' ';
            }
            else {
                of << Vel[j-1][i-1] << ' ';
            }
        }
        of << endl;
    }
    /// Write V velocities
    of << "V velocity field:" << endl;
    wrap(local.V, Nyr, Nxr, Vel);
    for (int j = 0; j < Ny; j++) {
        for (int i = 0; i < Nx; i++) {
            if (j == 0 || i == 0 || j == Ny-1 || i == Nx-1) {
                of << 0 << ' ';
            }
            else {
                of << Vel[j-1][i-1] << ' ';
            }
        }
        of << endl;
    }
    of.close();

    /// Delete 2D temp pointer
    for (int j = 0; j < Nyr; j++) {
        delete[] Vel[j];
    }
    delete[] Vel;
}

/**
 * @brief Calculates and sets energy of each velocity field per timestamp
 * */
void Burgers::SetEnergy() {
    /// Get Model parameters
    int Ny = model->GetNy();
    int Nx = model->GetNx();
    double dx = model->GetDx();
    double dy = model->GetDy();

    /// Reduced parameters
    int Nyr = Ny - 2;
    int Nxr = Nx - 2;

    /// Calculate Energy
    double ddotU = F77NAME(ddot)(Nyr*Nxr, local.U, 1, local.U, 1);
    double ddotV = F77NAME(ddot)(Nyr*Nxr, local.V, 1, local.V, 1);
    E = 0.5 * (ddotU + ddotV) * dx*dy;
}

/**
 * @brief Private helper function that computes and returns next velocity state based on previous inputs
 * @param Ui U velocity per timestamp
 * @param Vi V velocity per timestamp
 * @param SELECT_U true if the computation is for U
 * */
void Burgers::NextVelocityState(double* NextVel, bool SELECT_U) {
    /// Set aliases for computation
    double* Vel = (SELECT_U) ? local.U : local.V;
    double* Other = (SELECT_U)? local.V : local.U;

    SetLinearTerms(Vel, Other, NextVel, SELECT_U);
    // SetNonLinearTerms(Vel, Other, NextVel, SELECT_U);
}

void Burgers::SetLinearTerms(double* Vel, double* Other, double* NextVel, bool SELECT_U) {
    int Nxr = model->GetNx() - 2;
    int Nyr = model->GetNy() - 2;
    double alpha_dx_2 = model->GetAlphaDx_2();
    double beta_dx_2 = model->GetBetaDx_2();
    double alpha_dy_2 = model->GetAlphaDy_2();
    double beta_dy_2 = model->GetBetaDy_2();
    double alpha_dx_1 = model->GetAlphaDx_1();
    double beta_dx_1 = model->GetBetaDx_1();
    double alpha_dy_1 = model->GetAlphaDy_1();
    double beta_dy_1 = model->GetBetaDy_1();
    double dt = model->GetDt();
    double bdx = model->GetBDx();
    double bdy = model->GetBDy();
    const int blocksize = 8;

    // loop blocking + pre-fetching previous & next column from memory
    double* Vel_iMinus = nullptr;
    double* Vel_iPlus = nullptr;
//    double* VelPtr = nullptr;
//    double* NextVelPtr = nullptr;
//    int i, j, i2, j2, ii2, jj2;
//    for (i = 1; i < Nxr-1; i+=blocksize) {
//        for (j = 1; j < Nyr-1; j+=blocksize) {
//            for (i2 = 0, ii2 = i+i2, VelPtr = &Vel[i*Nyr+j], NextVelPtr = &NextVel[i*Nyr+j]; ii2 < Nxr && i2 < blocksize; ++i2, ++ii2, VelPtr += Nyr, NextVelPtr+=Nyr) {
//                if (ii2 > 0) Vel_iMinus = &Vel[(ii2-1)*Nyr+j];
//                if (ii2 < Nxr-1) Vel_iPlus = &Vel[(ii2+1)*Nyr+j];
//                for (j2 = 0, jj2 = j+j2; jj2 < Nyr && j2 < blocksize; ++j2, ++jj2) {
//                    // Update x
//                    NextVelPtr[j2] = alpha_dx_1 * VelPtr[j2] + beta_dx_1 * Vel_iMinus[j2];
//                    NextVelPtr[j2] = NextVelPtr[j2] + alpha_dx_2 * VelPtr[j2] + beta_dx_2 * Vel_iMinus[j2];
//                    NextVelPtr[j2] = NextVelPtr[j2] + beta_dx_2 * Vel_iPlus[j2];
//                    // Update y
//                    NextVelPtr[j2] = NextVelPtr[j2] + alpha_dy_1 * VelPtr[j2] + beta_dy_1 * VelPtr[j2-1];
//                    NextVelPtr[j2] = NextVelPtr[j2] + alpha_dy_2 * VelPtr[j2] + beta_dy_2 * VelPtr[j2-1];
//                    NextVelPtr[j2] = NextVelPtr[j2] + beta_dy_2 * VelPtr[j2+1];
//                }
//            }
//        }
//    }
//    // Update boundary conditions
//    // 4 corners
//    for (int i = 0; i < Nxr; i+=Nxr-1) {
//        for (int j = 0; j < Nyr; j+= Nyr-1) {
//            int curr = i*Nyr+j;
//            NextVel[curr] *= Vel[curr] * (alpha_dx_1 + alpha_dx_2 + alpha_dy_1 + alpha_dy_2);
//        }
//    }
//    // up
//    for (int i = 1; i < Nxr-1; i++) {
//        int upIdx = i * Nyr;
//        int leftIdx = (i-1)*Nyr;
//        int rightIdx = (i+1)*Nyr;
//        NextVel[upIdx] = alpha_dy_1 * Vel[upIdx] + alpha_dy_2 * Vel[upIdx];
//        NextVel[upIdx] += beta_dy_2 * Vel[upIdx+1]; // down contribution
//        NextVel[upIdx] += alpha_dx_1 * Vel[upIdx] + beta_dx_1 * Vel[leftIdx]; // left contribution
//        NextVel[upIdx] += beta_dx_2 * Vel[rightIdx]; // right contribution
//    }
//    // down
//    for (int i = 1; i < Nxr-1; i++) {
//        int downIdx = i * Nyr + (Nyr-1);
//        int leftIdx = downIdx - Nyr;
//        int rightIdx = downIdx + Nyr;
//        NextVel[downIdx] = alpha_dy_1 * Vel[downIdx] + alpha_dy_2 * Vel[downIdx];
//        NextVel[downIdx] += beta_dy_1 * Vel[downIdx-1] + beta_dy_2 * Vel[downIdx-1]; // up contribution
//        NextVel[downIdx] += alpha_dx_1 * Vel[downIdx] + beta_dx_1 * Vel[leftIdx]; // left contribution
//        NextVel[downIdx] += beta_dx_2 * Vel[rightIdx]; // right contribution
//    }
//    // left
//    for (int j = 1; j < Nyr-1; j++) {
//        int rightIdx = j + Nyr;
//        NextVel[j] = alpha_dx_1 * Vel[j] + alpha_dx_2 * Vel[j];
//        NextVel[j] += beta_dx_2 * Vel[rightIdx]; // right contribution
//        NextVel[j] += beta_dy_2 * Vel[j+1]; // down contribution
//        NextVel[j] += beta_dy_1 * Vel[j-1] + beta_dy_2 * Vel[j-1]; // up contribution
//    }
//    // right
//    for (int j = 1; j < Nyr-1; j++) {
//        int rightIdx = Nyr*(Nxr-1)+j;
//        int leftIdx = rightIdx - Nyr;
//        NextVel[rightIdx] = alpha_dx_1 * Vel[rightIdx] + alpha_dx_2 * Vel[rightIdx];
//        NextVel[rightIdx] += beta_dy_2 * Vel[rightIdx+1]; // down contribution
//        NextVel[rightIdx] += beta_dx_2 * Vel[leftIdx] + beta_dx_1 * Vel[leftIdx]; // left contribution
//        NextVel[rightIdx] += beta_dy_1 * Vel[rightIdx-1] + beta_dy_2 * Vel[rightIdx-1]; // up contribution
//    }
    double Vel_Vel, Vel_Other, Vel_Vel_Minus_1, Vel_Other_Minus_1;
    for (int i = 0; i < Nxr; i++) {
        if (i > 0) Vel_iMinus = &(Vel[(i-1)*Nyr]);
        if (i < Nxr-1) Vel_iPlus = &(Vel[(i+1)*Nyr]);
        int start = i*Nyr;
        for (int j = 0; j < Nyr; j+=blocksize) {
            for (int k = j; k < Nyr && k < j + blocksize; k++) {
                int curr = start + k;
                // Update x
                NextVel[curr] = (i > 0) ? alpha_dx_1 * Vel[curr] + beta_dx_1 * Vel_iMinus[k] :alpha_dx_1 * Vel[curr];
                NextVel[curr] = (i > 0) ? NextVel[curr] + alpha_dx_2 * Vel[curr] + beta_dx_2 * Vel_iMinus[k] :NextVel[curr] + alpha_dx_2 * Vel[curr];
                NextVel[curr] = (i < Nxr-1) ? NextVel[curr] + beta_dx_2 * Vel_iPlus[k] : NextVel[curr];
                // Update y
                NextVel[curr] = (k > 0) ? NextVel[curr] + alpha_dy_1 * Vel[curr] + beta_dy_1 * Vel[curr-1] : NextVel[curr] + alpha_dy_1 * Vel[curr];
                NextVel[curr] = (k > 0) ? NextVel[curr] + alpha_dy_2 * Vel[curr] + beta_dy_2 * Vel[curr-1] : NextVel[curr] + alpha_dy_2 * Vel[curr];
                NextVel[curr] = (k < Nyr-1) ? NextVel[curr] + beta_dy_2 * Vel[curr+1] : NextVel[curr];
                switch (SELECT_U) {
                    case true:
                        Vel_Vel = bdx * Vel[curr] * Vel[curr];
                        Vel_Other = bdy * Vel[curr] * Other[curr];
                        Vel_Vel_Minus_1 = (i == 0) ? 0 : bdx * Vel_iMinus[k] * Vel[curr];
                        Vel_Other_Minus_1 = (k == 0) ? 0 : bdy * Vel[curr - 1] * Other[curr];
                        NextVel[curr] -= (Vel_Vel + Vel_Other - Vel_Vel_Minus_1 - Vel_Other_Minus_1);
                        NextVel[curr] *= dt;
                        NextVel[curr] += Vel[curr];
                        break;
                    case false:
                        Vel_Vel = bdy * Vel[curr] * Vel[curr];
                        Vel_Other = bdx * Vel[curr] * Other[curr];
                        Vel_Vel_Minus_1 = (k == 0) ? 0 : bdy * Vel[curr - 1] * Vel[curr];
                        Vel_Other_Minus_1 = (i == 0) ? 0 : bdx * Vel_iMinus[k] * Other[curr];
                        NextVel[curr] -= (Vel_Vel + Vel_Other - Vel_Vel_Minus_1 - Vel_Other_Minus_1);
                        NextVel[curr] *= dt;
                        NextVel[curr] += Vel[curr];
                        break;
                }
            }
        }
    }
}

void Burgers::SetNonLinearTerms(double* Vel, double* Other, double* NextVel, bool SELECT_U) {
    /// Get model parameters
    int Nyr = model->GetNy() - 2;
    int Nxr = model->GetNx() - 2;
    double dt = model->GetDt();
    double bdx = model->GetBDx();
    double bdy = model->GetBDy();
    const int blocksize = 8;

    double* Vel_iMinus = nullptr;
    double Vel_Vel, Vel_Other, Vel_Vel_Minus_1, Vel_Other_Minus_1;
    if (SELECT_U) {
        for (int i = 0; i < Nxr; i++) {
            if (i > 0) Vel_iMinus = &(Vel[(i-1)*Nyr]);
            int start = i*Nyr;
            for (int j = 0; j < Nyr; j+=blocksize) {
                for (int k = j; k < Nyr && k < j + blocksize; k++) {
                    int curr = start + k;
                    Vel_Vel = bdx * Vel[curr] * Vel[curr];
                    Vel_Other = bdy * Vel[curr] * Other[curr];
                    Vel_Vel_Minus_1 = (i == 0) ? 0 : bdx * Vel_iMinus[k] * Vel[curr];
                    Vel_Other_Minus_1 = (k == 0) ? 0 : bdy * Vel[curr - 1] * Other[curr];
                    NextVel[curr] -= (Vel_Vel + Vel_Other - Vel_Vel_Minus_1 - Vel_Other_Minus_1);
                    NextVel[curr] *= dt;
                    NextVel[curr] += Vel[curr];
                }
            }
        }
    }
    else {
        for (int i = 0; i < Nxr; i++) {
            if (i > 0) Vel_iMinus = &(Vel[(i-1)*Nyr]);
            int start = i*Nyr;
            for (int j = 0; j < Nyr; j+=blocksize) {
                for (int k = j; k < Nyr && k < j + blocksize; k++) {
                    int curr = start + k;
                    Vel_Vel = bdy * Vel[curr] * Vel[curr];
                    Vel_Other = bdx * Vel[curr] * Other[curr];
                    Vel_Vel_Minus_1 = (k == 0) ? 0 : bdy * Vel[curr - 1] * Vel[curr];
                    Vel_Other_Minus_1 = (i == 0) ? 0 : bdx * Vel_iMinus[k] * Other[curr];
                    NextVel[curr] -= (Vel_Vel + Vel_Other - Vel_Vel_Minus_1 - Vel_Other_Minus_1);
                    NextVel[curr] *= dt;
                    NextVel[curr] += Vel[curr];
                }
            }
        }
    }
}
