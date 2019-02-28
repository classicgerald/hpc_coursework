#include <cmath>
#include <fstream>
#include <iomanip>
#include <mpi.h>
#include "BLAS_Wrapper.h"
#include "Helpers.h"
#include "Burgers2P.h"

using namespace std;

/**
 * @brief Public Constructor: Accepts a Model instance reference as input
 * @brief Allocates memory to all other instance variables
 * @param &m reference to Model instance
 * */
Burgers2P::Burgers2P(Model &m) {
    /// Set model class pointer as instance variable
    model = &m;

    /// Get model parameters
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();

    /// Allocate memory to instance variables
    U = new double[Nyr*Nxr];
    V = new double[Nyr*Nxr];

    /// Matrix coefficients
    dVel_dx_2_coeffs = new double[Nxr*Nxr];
    dVel_dy_2_coeffs = new double[Nyr*Nyr];
    dVel_dx_coeffs = new double[Nxr*Nxr];
    dVel_dy_coeffs = new double[Nyr*Nyr];

    /// Caches
    upVel = new double[Nxr];
    downVel = new double[Nxr];
    leftVel = new double[Nyr];
    rightVel = new double[Nyr];
}

/**
 * @brief Destructor: Deletes all allocated pointers in the class instance
 * */
Burgers2P::~Burgers2P() {
    /// Delete U and V
    delete[] U;
    delete[] V;

    /// Delete Caches
    delete[] upVel;
    delete[] downVel;
    delete[] leftVel;
    delete[] rightVel;

    /// Delete matrix coefficients
    delete[] dVel_dx_2_coeffs;
    delete[] dVel_dy_2_coeffs;
    delete[] dVel_dx_coeffs;
    delete[] dVel_dy_coeffs;

    /// model is not dynamically alloc
}

/**
 * @brief Sets initial velocity field in x,y for U0 (V0 = U0)
 * */
void Burgers2P::SetInitialVelocity() {
    /// Get model parameters
    double x0 = model->GetX0();
    double y0 = model->GetY0();
    double dx = model->GetDx();
    double dy = model->GetDy();
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();
    int displ_x = model->GetDisplX();
    int displ_y = model->GetDisplY();

    /// Compute U0
    /// Memory layout in column-major format
    double loc_x0 = x0 + (displ_x+1)*dx;
    double loc_y0 = y0 - (displ_y+1)*dy;
    for (int i = 0; i < Nxr; i++) {
        for (int j = 0; j < Nyr; j++) {
            double x = loc_x0 + i*dx;
            double y = loc_y0 - j*dy;
            double r = ComputeR(x, y);
            U[i*Nyr+j] = (r <= 1.0)? 2.0*pow(1.0-r,4.0) * (4.0*r+1.0) : 0.0;
            V[i*Nyr+j] = (r <= 1.0)? 2.0*pow(1.0-r,4.0) * (4.0*r+1.0) : 0.0;
        }
    }
}

/**
 * @brief Sets velocity field in x,y for U, V
 * */
void Burgers2P::SetIntegratedVelocity() {
    /// Get model parameters
    int Nt = model->GetNt();

    /// Set Matrix Coefficients
    SetMatrixCoefficients();


    /// Compute U, V for every step k
    for (int k = 0; k < Nt-1; k++) {
        double* NextU = NextVelocityState(U, V, true);
        double* NextV = NextVelocityState(U, V, false);
        CopyAndDelete(NextU, NextV);
    }
}

/**
 * @brief Writes the velocity field for U, V into a file
 * IMPORTANT: Run SetIntegratedVelocity() first
 * */
void Burgers2P::WriteVelocityFile() {
    /// Get model parameters
    int Ny = model->GetNy();
    int Nx = model->GetNx();

    /// Allocate 2D pointer
    double** M = new double*[Ny-2];
    for (int j = 0; j < Ny-2; j++) {
        M[j] = new double[Nx-2];
    }

    /// Open output file stream to data.txt
    ofstream of;
    of.open("data.txt", ios::out | ios::trunc);
    of.precision(4); // 4 s.f.

    /// Write U velocity
    WriteOf(U, M, of, 'U');

    /// Write V velocity
    WriteOf(V, M, of, 'V');
    of.close();

    /// Delete 2D pointer
    for (int j = 0; j < Ny-2; j++) {
        delete[] M[j];
    }
    delete[] M;
}

/**
 * @brief Private helper function to write to output stream
 * @param Vel pointer to either U or V
 * @param M 2D pointer representing global matrix (should have been allocated memory)
 * @param &of reference to output file stream
 * @param id Supply 'U' or 'V'
 * */
void Burgers2P::WriteOf(double* Vel, double** M, ofstream &of, char id) {
    int loc_rank = model->GetRank();
    int Ny = model->GetNy();
    int Nx = model->GetNx();

    AssembleMatrix(Vel, M);
    if (loc_rank == 0) {
        of << id << " velocity field:" << endl;
        for (int j = 0; j < Ny; j++) {
            for (int i = 0; i < Nx; i++) {
                if (j == 0 || i == 0 || j == Ny - 1 || i == Nx - 1) {
                    of << 0 << ' ';
                } else {
                    of << M[j - 1][i - 1] << ' ';
                }
            }
            of << endl;
        }
    }
}

/**
 * @brief Calculates and sets energy of velocity field
 * */
void Burgers2P::SetEnergy() {
    E = CalculateEnergyState(U, V);
}

/**
 * @brief Private helper function that calculates next energy state per timestamp
 * @param Ui U velocity per timestamp (i.e. supply U[k])
 * @param Vi V velocity per timestamp (i.e. supply V[k])
 * */
double Burgers2P::CalculateEnergyState(double* Ui, double* Vi) {
    /// Get model parameters
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();
    double dx = model->GetDx();
    double dy = model->GetDy();
    MPI_Comm vu = model->GetComm();

    /// Blas calls to compute dot products
    double loc_ddotU = F77NAME(ddot)(Nyr*Nxr, Ui, 1, Ui, 1);
    double loc_ddotV = F77NAME(ddot)(Nyr*Nxr, Vi, 1, Vi, 1);

    /// Compute local energy state
    double NextLocalEnergyState = 0.5 * (loc_ddotU + loc_ddotV) * dx*dy;
    double NextGlobalEnergyState;

    /// Sum into global energy state
    MPI_Allreduce(&NextLocalEnergyState, &NextGlobalEnergyState, 1, MPI_DOUBLE, MPI_SUM, vu);
    return NextGlobalEnergyState;
}

/**
 * @brief Private helper function that computes and returns next velocity state based on previous inputs
 * @param Ui U velocity per timestamp (i.e. supply U[k])
 * @param Vi V velocity per timestamp (i.e. supply V[k])
 * @param SELECT_U true if the computation is for U
 * */
double* Burgers2P::NextVelocityState(double* Ui, double* Vi, bool SELECT_U) {
    /// Get model parameters
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();
    double dt = model->GetDt();
    double dx = model->GetDx();
    double dy = model->GetDy();
    double b = model->GetB();

    /// Set aliases for computation
    double* Vel = (SELECT_U) ? Ui : Vi;
    double* Other = (SELECT_U)? Vi : Ui;

    /// Set caches for Vel
    SetCaches(Vel);

    /// Generate term arrays
    double* NextVel = new double[Nyr*Nxr];
    double* dVel_dx_2 = new double[Nyr*Nxr];
    double* dVel_dy_2 = new double[Nyr*Nxr];
    double* dVel_dx = new double[Nyr*Nxr];
    double* dVel_dy = new double[Nyr*Nxr];
    double* Vel_Vel = nullptr;
    double* Vel_Other = nullptr;
    double* Vel_Vel_Minus_1 = nullptr;
    double* Vel_Other_Minus_1 = nullptr;

    /// Compute second derivatives
    F77NAME(dsymm)('R', 'U', Nyr, Nxr, 1.0, dVel_dx_2_coeffs, Nxr, Vel, Nyr, 0.0, dVel_dx_2, Nyr);
    F77NAME(dsymm)('L', 'U', Nyr, Nxr, 1.0, dVel_dy_2_coeffs, Nyr, Vel, Nyr, 0.0, dVel_dy_2, Nyr);

    /// Compute first derivatives
    F77NAME(dcopy)(Nyr*Nxr, Vel, 1, dVel_dx, 1);
    F77NAME(dcopy)(Nyr*Nxr, Vel, 1, dVel_dy, 1);
    F77NAME(dtrmm)('R', 'U', 'N', 'N', Nyr, Nxr, 1.0, dVel_dx_coeffs, Nxr, dVel_dx, Nyr);
    F77NAME(dtrmm)('L', 'L', 'N', 'N', Nyr, Nxr, 1.0, dVel_dy_coeffs, Nyr, dVel_dy, Nyr);

    /// Compute b terms
    if (SELECT_U) {
        Vel_Vel = MatMul(Vel, Vel, Nyr, Nxr, false, false, b/dx);
        Vel_Other = MatMul(Vel, Other, Nyr, Nxr, false, false, b/dy);
        Vel_Vel_Minus_1 = MatMul(Vel, Vel, Nyr, Nxr, true, false, b/dx);
        Vel_Other_Minus_1 = MatMul(Vel, Other, Nyr, Nxr, false, true, b/dy);


    }
    else {
        Vel_Vel = MatMul(Vel, Vel, Nyr, Nxr, false, false, b/dy);
        Vel_Other = MatMul(Vel, Other, Nyr, Nxr, false, false, b/dx);
        Vel_Vel_Minus_1 = MatMul(Vel, Vel, Nyr, Nxr, false, true, b/dy);
        Vel_Other_Minus_1 = MatMul(Vel, Other, Nyr, Nxr, true, false, b/dx);
    }

    UpdateBoundsLinear(dVel_dx_2, dVel_dy_2, dVel_dx, dVel_dy);
    UpdateBoundsNonLinear(Vel, Other, Vel_Vel_Minus_1, Vel_Other_Minus_1, SELECT_U);

    /// Matrix addition through all terms
    for (int i = 0; i < Nyr*Nxr; i++) {
        NextVel[i] = dVel_dx_2[i] + dVel_dy_2[i] - dVel_dx[i] - dVel_dy[i] -
                (Vel_Vel[i] + Vel_Other[i] - Vel_Vel_Minus_1[i] - Vel_Other_Minus_1[i]);
        NextVel[i] *= dt;
        NextVel[i] += Vel[i];
    }

    /// Delete term array pointers
    delete[] dVel_dx_2;
    delete[] dVel_dy_2;
    delete[] dVel_dx;
    delete[] dVel_dy;
    delete[] Vel_Vel;
    delete[] Vel_Other;
    delete[] Vel_Vel_Minus_1;
    delete[] Vel_Other_Minus_1;

    return NextVel;
}

/**
 * @brief Copies next state velocity into previous state, and deletes the temporary pointer
 * */
void Burgers2P::CopyAndDelete(double* NextU, double* NextV) {
    /// Get model parameters
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();
    for (int i = 0; i < Nyr*Nxr; i++) {
        U[i] = NextU[i];
        V[i] = NextV[i];
    }
    delete[] NextU;
    delete[] NextV;
}
/**
 * @brief Private helper function that sets matrix coefficients for differentials
 * */
void Burgers2P::SetMatrixCoefficients() {
    /// Get model parameters
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();
    double dx = model->GetDx();
    double dy = model->GetDy();
    double ax = model->GetAx();
    double ay = model->GetAy();
    double c = model->GetC();

    /// Set coefficients
    GenSymm((-2.0*c)/pow(dx,2.0), c/pow(dx,2.0), Nxr, Nxr, dVel_dx_2_coeffs);
    GenSymm((-2.0*c)/pow(dy,2.0), c/pow(dy,2.0), Nyr, Nyr, dVel_dy_2_coeffs);
    GenTrmm(ax/dx, -ax/dx, Nxr, Nxr, true, dVel_dx_coeffs);
    GenTrmm(ay/dy, -ay/dy, Nyr, Nyr, false, dVel_dy_coeffs);
}

/**
 * @brief Private helper function that sets the boundary condition velocities
 * @param Vel pointer to U[k] or V[k]
 * */
void Burgers2P::SetCaches(double* Vel) {
    /// Get model parameters
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();

    /// Get ranks
    int up = model->GetUp();
    int down = model->GetDown();
    int left = model->GetLeft();
    int right = model->GetRight();


    /// Get communicator
    MPI_Comm vu = model->GetComm();
    int flag;

    /// Allocate memory into Vel for local bounds
    double* myUpVel = new double[Nxr];
    double* myDownVel = new double[Nxr];
    double* myLeftVel = new double[Nyr];
    double* myRightVel = new double[Nyr];

    /// Get Vel bounds for this sub-matrix
    for (int i = 0; i < Nxr; i++) {
        myUpVel[i] = Vel[i*Nyr];
        myDownVel[i] = Vel[i*Nyr+(Nyr-1)];
    }
    for (int j = 0; j < Nyr; j++) {
        myLeftVel[j] = Vel[j];
        myRightVel[j] = Vel[(Nxr-1)*Nyr+j];
    }

    /// Exchange up/down
    flag = 0;
    /* Send down boundary to down and receive into up boundary */
    MPI_Sendrecv(myDownVel, Nxr, MPI_DOUBLE, down, flag, upVel, Nxr, MPI_DOUBLE, up, flag, vu, MPI_STATUS_IGNORE);
    /* Send up boundary to up and receive into down boundary */
    MPI_Sendrecv(myUpVel, Nxr, MPI_DOUBLE, up, flag, downVel, Nxr, MPI_DOUBLE, down, flag, vu, MPI_STATUS_IGNORE);

    /// Exchange left/right
    flag = 1;
    /* Send right boundary to right and receive into left boundary */
    MPI_Sendrecv(myRightVel, Nyr, MPI_DOUBLE, right, flag, leftVel, Nyr, MPI_DOUBLE, left, flag, vu, MPI_STATUS_IGNORE);
    /* Send left boundary to left and receive into right boundary */
    MPI_Sendrecv(myLeftVel, Nyr, MPI_DOUBLE, left, flag, rightVel, Nyr, MPI_DOUBLE, right, flag, vu, MPI_STATUS_IGNORE);

    /// Handle boundaries
    if (up < 0) SetZeroes(upVel, Nxr);
    if (down < 0) SetZeroes(downVel, Nxr);
    if (left < 0) SetZeroes(leftVel, Nyr);
    if (right < 0) SetZeroes(rightVel, Nyr);

    delete[] myUpVel;
    delete[] myDownVel;
    delete[] myLeftVel;
    delete[] myRightVel;
}

/**
 * @brief Private helper function that updates the linear boundary conditions for the program's sub-matrix
 * @param dVel_dx_2 pointer to dVel_dx_2 in NextVelocityState()
 * @param dVel_dy_2 pointer to dVel_dy_2 in NextVelocityState()
 * @param dVel_dx pointer to dVel_dx in NextVelocityState()
 * @param dVel_dy pointer to dVel_dy in NextVelocityState()
 * */
void Burgers2P::UpdateBoundsLinear(double* dVel_dx_2, double* dVel_dy_2, double* dVel_dx, double* dVel_dy) {
    /// Get model parameters
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();
    double dx = model->GetDx();
    double dy = model->GetDy();
    double ax = model->GetAx();
    double ay = model->GetAy();
    double c = model->GetC();

    /// Modify boundaries on this sub-matrix

    /// Fix left and right boundaries
    for (int j = 0; j < Nyr; j++) {
        // left
        dVel_dx_2[j] += c/pow(dx,2.0)*leftVel[j];
        dVel_dx[j] -= ax/dx*leftVel[j];

        // right
        dVel_dx_2[(Nxr-1)*Nyr+j] += c/pow(dx,2.0)*rightVel[j];
    }

    /// Fix up and down boundaries
    for (int i = 0; i < Nxr; i++) {
        // up
        dVel_dy_2[i*Nyr] += c/pow(dy,2.0)*upVel[i];
        dVel_dy[i*Nyr] -= ay/dy*upVel[i];

        // down
        dVel_dy_2[i*Nyr+(Nyr-1)] += c/pow(dy,2.0)*downVel[i];
    }
}

/**
 * @brief Private helper function that updates the non-linear boundary conditions for the program's sub-matrix
 * @param Vel pointer to Vel in NextVelocityState()
 * @param Other pointer to Other in NextVelocityState()
 * @param Vel_Vel_Minus_1 pointer to Vel_Vel_Minus_1 in NextVelocityState()
 * @param Vel_Other_Minus_1 pointer to Vel_Other_Minus_1 in NextVelocityState()
 * @param SELECT_U true if the computation is for U
 * */
void Burgers2P::UpdateBoundsNonLinear(double* Vel, double* Other, double* Vel_Vel_Minus_1, double* Vel_Other_Minus_1, bool SELECT_U) {
    /// Get model parameters
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();
    double dx = model->GetDx();
    double dy = model->GetDy();
    double b = model->GetB();

    if (SELECT_U) {
        // up
        for (int i = 0; i < Nxr; i++) {
            Vel_Other_Minus_1[i*Nyr] = b/dy * upVel[i] * Other[i*Nyr];
        }
        // left
        for (int j = 0; j < Nyr; j++) {
            Vel_Vel_Minus_1[j] = b/dx * leftVel[j] * Vel[j];
        }
    }
    else {
        // up
        for (int i = 0; i < Nxr; i++) {
            Vel_Vel_Minus_1[i*Nyr] = b/dy * upVel[i] * Vel[i*Nyr];
        }
        // left
        for (int j = 0; j < Nyr; j++) {
            Vel_Other_Minus_1[j] = b/dx * leftVel[j] * Other[j];
        }
    }
}

/**
 * @brief Private helper function that assembles the global matrix into a preallocated M
 * @brief Arranges data into row-major format from a column-major format in the 1D pointer Vel
 * @param Vel 1D pointer to Vel in column-major format
 * @param M 2D pointer (pre-allocated memory) to be filled in row-major format
 * */
void Burgers2P::AssembleMatrix(double* Vel, double** M) {
    /// Get model parameters
    int loc_rank = model->GetRank();
    int Ny = model->GetNy();
    int Nx = model->GetNx();
    int Nyr = model->GetLocNyr();
    int Nxr = model->GetLocNxr();
    int Px = model->GetPx();
    int Py = model->GetPy();
    MPI_Comm vu = model->GetComm();

    /// Don't delete these pointers (Part of Model object)
    int* displs = model->GetDispls();
    int* recvcount = model->GetRecvCount();
    int* rankNxrMap = model->GetRankNxrMap();
    int* rankNyrMap = model->GetRankNyrMap();
    int* rankDisplsXMap = model->GetRankDisplsXMap();
    int* rankDisplsYMap = model->GetRankDisplsYMap();

    /// Gather into globalVel in root (rank == 0)
    double* globalVel = new double[(Ny-2)*(Nx-2)];
    MPI_Gatherv(Vel, Nyr*Nxr, MPI_DOUBLE, globalVel, recvcount, displs, MPI_DOUBLE, 0, vu);

    /// Build global matrix in root, convert column-major -> row-major format
    if (loc_rank == 0) {
        for (int k = 0; k < Px*Py; k++) {
            for (int i = 0; i < rankNxrMap[k]; i++) {
                for (int j = 0; j < rankNyrMap[k]; j++) {
                    int loc_displ_y = rankDisplsYMap[k];
                    int loc_displ_x = rankDisplsXMap[k];
                    int loc_Nyr = rankNyrMap[k];
                    int global_displ = displs[k];
                    M[loc_displ_y+j][loc_displ_x+i] = globalVel[global_displ+i*loc_Nyr+j];
                }
            }
        }
    }

    delete[] globalVel;
}

/**
 * @brief Private helper function computing R for SetInitialVelocity()
 * @param x real x distance to origin
 * @param y real y distance to origin
 * */
double Burgers2P::ComputeR(double x, double y) {
    double r = pow(x*x+y*y, 0.5);
    return r;
}