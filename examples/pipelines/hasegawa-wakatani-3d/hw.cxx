
#include <bout/physicsmodel.hxx>
#include <smoothing.hxx>
#include <invert_laplace.hxx>
#include <derivs.hxx>

#include <bout/pipelines.hxx>
using namespace bout::experimental;

class HW3D : public PhysicsModel {
private:
  Field3D n, vort;  // Evolving density and vorticity
  Field3D phi;      // Electrostatic potential

  // Model parameters
  BoutReal alpha;      // Adiabaticity (~conductivity)
  BoutReal kappa;      // Density gradient drive
  BoutReal Dvort, Dn;  // Diffusion
  
  std::unique_ptr<Laplacian> phiSolver; // Laplacian solver for vort -> phi

protected:
  int init(bool UNUSED(restart)) {

    auto& options = Options::root()["hw"];
    alpha = options["alpha"].withDefault(1.0);
    kappa = options["kappa"].withDefault(0.1);
    Dvort = options["Dvort"].doc("Vorticity diffusion (normalised)").withDefault(1e-2);
    Dn = options["Dn"].doc("Density diffusion (normalised)").withDefault(1e-2);

    SOLVE_FOR(n, vort);
    SAVE_REPEAT(phi);
    
    phiSolver = Laplacian::create();
    phi = 0.; // Starting phi
    
    return 0;
  }

  int rhs(BoutReal UNUSED(time)) {
    // Solve for potential
    phi = phiSolver->solve(vort, phi);
    
    Field3D phi_minus_n = phi - n;
    
    // Communicate variables
    mesh->communicate(n, vort, phi, phi_minus_n);

    // Make sure fields have Coordinates
    // This sets the Field::fast_coords member to a Coordinate*
    // Not a long-term solution, but here until a better solution is found.
    n.fast_coords = n.getCoordinates();
    vort.fast_coords = vort.getCoordinates();
    phi.fast_coords = phi.getCoordinates();
    phi_minus_n.fast_coords = phi_minus_n.getCoordinates();

    // Density equation
    ddt(n) = bp::bracket(n, phi)
             | subtract(bp::Div_par_Grad_par(phi_minus_n) | multiply(alpha))
             | subtract(bp::DDZ(phi) | multiply(kappa))
             | add(bp::Delp2(n) | multiply(Dn))
             | toField(n.getRegion("RGN_NOBNDRY"));

    // Vorticity equation
    ddt(vort) = bp::bracket(vort, phi)
                | subtract(bp::Div_par_Grad_par(phi_minus_n) | multiply(alpha))
                | add(bp::Delp2(vort) | multiply(Dvort))
                | toField(vort.getRegion("RGN_NOBNDRY"));

    return 0;
  }
};

// Define a main() function
BOUTMAIN(HW3D);
