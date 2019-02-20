#include <iostream>

#include "Model.h"
#include "ParseException.h"

/**
 * CPP source file for Model class
 * Constructors, public and private member functions defined here
 * */

using namespace std;

/**
 * Constructor: sets constants from arg parameters
 * */
Model::Model(int argc, char** argv) {
    try {
        ParseParameters(argc, argv);
    } catch (IllegalArgumentException &e) {
        cout << e.what() << endl;
    }
    // validate then SetNumerics
    ValidateParameters();
}

Model::~Model() {}

/**
 * Parses parameters from command line into program
 * */
void Model::ParseParameters(int argc, char **argv) {
    if (argc == 8) {
        ax = atof(argv[1]);
        ay = atof(argv[2]);
        b = atof(argv[3]);
        c = atof(argv[4]);
        Lx = atof(argv[5]);
        Ly = atof(argv[6]);
        T = atof(argv[7]);
        cout << "Parameters saved successfully." << endl;
    }
    else throw illegalArgumentException;
}

/**
 * Prints model parameters
 * */
void Model::PrintParameters() {
    cout << "ax: " << ax << endl;
    cout << "ay: " << ay << endl;
    cout << "b: " << b << endl;
    cout << "c: " << c << endl;
    cout << "Lx: " << Lx << endl;
    cout << "Ly: " << Ly << endl;
    cout << "T: " << T << endl;
}

/**
 * Checks if parameters supplied are valid
 * */
bool Model::IsValid() {
    return ax >= 0 && ay >= 0 && b >= 0 && c >= 0 && Lx >= 0 && Ly >= 0 && T >= 0;
}

void Model::ValidateParameters() {
    if (!IsValid()) cout << "WARN: Parameter values have to be (>=0)" << endl;
    else SetNumerics();
}

/**
 * Set appropriate values for various members
 * */
void Model::SetNumerics() {
    Nx = 10;
    Ny = 10;
    Nt = 10;
    // dx,dy and dt are dependent on L,T and Nx,Ny,Nt:
    dx = Lx / Nx;
    dy = Ly / Ny;
    dt = T / Nt;
    // x0 and y0 represent the top LHS of the matrix:
    x0 = -Lx/2.0;
    y0 = Ly/2.0;
}