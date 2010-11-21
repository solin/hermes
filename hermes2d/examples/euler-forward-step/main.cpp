#define HERMES_REPORT_INFO
#define HERMES_REPORT_FILE "application.log"
#include "hermes2d.h"

//  This example solves the compressible Euler equations using a basic
//  piecewise-constant finite volume method.
//
//  Equations: Compressible Euler equations, perfect gas state equation.
//
//  Domain: forward facing step, see mesh file ffs.mesh
//
//  BC: Normal velocity component is zero on solid walls.
//      Full supersonic state prescribed at inlet.
//      Pressure given at outlet, but used only if outlet flow is subsonic/
//
//  IC: Constant supersonic state identical to inlet. 
//
//  The following parameters can be changed:
 
const int P_INIT = 0;                             // Initial polynomial degree.                      
const int INIT_REF_NUM = 2;                       // Number of initial uniform mesh refinements.                       
double TAU = 1E-3;                                // Time step.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_UMFPACK, SOLVER_PETSC, SOLVER_MUMPS, 
                                                  // SOLVER_PARDISO, SOLVER_SUPERLU, SOLVER_AMESOS, SOLVER_AZTECOO

// Equation parameters.
double P_EXT = 1.0;         // Exterior pressure (dimensionless).
double RHO_EXT = 1.4;       // Inlet density (dimensionless).   
double V1_EXT = 3.0;        // Inlet x-velocity (dimensionless).
double V2_EXT = 0.0;        // Inlet y-velocity (dimensionless).
double KAPPA = 1.4;         // Kappa.

double t = 0;

// Numerical flux.
// For numerical fluxes, please see hermes2d/src/numerical_flux.h
NumericalFlux num_flux(KAPPA);

// Boundary condition types.
BCType bc_types(int marker)
{
  return BC_NATURAL;
}

// Inlet/outlet boundary conditions.
double bc_density(double y)
{
  return RHO_EXT;
}

// Density * velocity in the x coordinate boundary condition.
double bc_density_vel_x(double y)
{
  return RHO_EXT * V1_EXT;
}

// Density * velocity in the y coordinate boundary condition.
double bc_density_vel_y(double y)
{
  return V2_EXT;
}

// Calculation of the pressure on the boundary.
double bc_pressure(double y)
{
  return P_EXT;
}

// Energy boundary condition.
double bc_energy(double y)
{
  double rho = bc_density(y);
  double rho_v_x = bc_density_vel_x(y);
  double rho_v_y = bc_density_vel_y(y);
  double pressure = bc_pressure(y);
  return pressure/(KAPPA - 1.) + (rho_v_x*rho_v_x+rho_v_y*rho_v_y) / 2*rho;
}

// Calculates energy from other quantities.
// FIXME: this should be in the src/ directory, not here.
double calc_energy(double rho, double rho_v_x, double rho_v_y, double pressure)
{
  return pressure/(num_flux.kappa - 1.) + (rho_v_x*rho_v_x+rho_v_y*rho_v_y) / 2*rho;
}

// Calculates pressure from other quantities.
// FIXME: this should be in the src/ directory, not here.
double calc_pressure(double rho, double rho_v_x, double rho_v_y, double energy)
{
  return (num_flux.kappa - 1.) * (energy - (rho_v_x*rho_v_x + rho_v_y*rho_v_y) / (2*rho));
}

// Calculates speed of sound.
// FIXME: this should be in the src/ directory, not here.
double calc_sound_speed(double rho, double rho_v_x, double rho_v_y, double energy)
{
  return std::sqrt(num_flux.kappa * calc_pressure(rho, rho_v_x, rho_v_y, energy) / rho);
}

// Constant initial state (matching the supersonic inlet state).
double ic_density(double x, double y, scalar& dx, scalar& dy)
{
  return RHO_EXT;
}
double ic_density_vel_x(double x, double y, scalar& dx, scalar& dy)
{
  return RHO_EXT * V1_EXT;
}
double ic_density_vel_y(double x, double y, scalar& dx, scalar& dy)
{
  return RHO_EXT * V2_EXT;
}
double ic_energy(double x, double y, scalar& dx, scalar& dy)
{
  return calc_energy(RHO_EXT, RHO_EXT*V1_EXT, RHO_EXT*V2_EXT, P_EXT);
}

// Weak forms.
#include "forms.cpp"

// Filters.
#include "filters.cpp"

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("ffs.mesh", &mesh);

  // Perform initial mesh refinements.
  for (int i = 0; i < INIT_REF_NUM; i++) mesh.refine_all_elements();

  // Initialize spaces with default shapesets.
  L2Space space_rho(&mesh,P_INIT);
  L2Space space_rho_v_x(&mesh,P_INIT);
  L2Space space_rho_v_y(&mesh,P_INIT);
  L2Space space_e(&mesh,P_INIT);

  // Set boundary condition types.
  space_rho.set_bc_types(bc_types);
  space_rho_v_x.set_bc_types(bc_types);
  space_rho_v_y.set_bc_types(bc_types);
  space_e.set_bc_types(bc_types);

  // Initialize solutions, set initial conditions.
  Solution sln_rho, sln_rho_v_x, sln_rho_v_y, sln_e, prev_rho, prev_rho_v_x, prev_rho_v_y, prev_e;
  sln_rho.set_exact(&mesh, ic_density);
  sln_rho_v_x.set_exact(&mesh, ic_density_vel_x);
  sln_rho_v_y.set_exact(&mesh, ic_density_vel_y);
  sln_e.set_exact(&mesh, ic_energy);
  prev_rho.set_exact(&mesh, ic_density);
  prev_rho_v_x.set_exact(&mesh, ic_density_vel_x);
  prev_rho_v_y.set_exact(&mesh, ic_density_vel_y);
  prev_e.set_exact(&mesh, ic_energy);

  // Initialize weak formulation.
  WeakForm wf(4);

  // Bilinear forms coming from time discretization by explicit Euler's method.
  wf.add_matrix_form(0,0,callback(bilinear_form_0_0_time));
  wf.add_matrix_form(1,1,callback(bilinear_form_1_1_time));
  wf.add_matrix_form(2,2,callback(bilinear_form_2_2_time));
  wf.add_matrix_form(3,3,callback(bilinear_form_3_3_time));

  // Volumetric linear forms.
  // Linear forms coming from the linearization by taking the Eulerian fluxes' Jacobian matrices from the previous time step.
  // First flux.
  wf.add_vector_form(0,callback(linear_form_0_1), HERMES_ANY, Tuple<MeshFunction*>(&prev_rho_v_x));
  wf.add_vector_form(1,callback(linear_form_1_0_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(1,callback(linear_form_1_1_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(1,callback(linear_form_1_2_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(1,callback(linear_form_1_3_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(2,callback(linear_form_2_0_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(2,callback(linear_form_2_1_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(2,callback(linear_form_2_2_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(2,callback(linear_form_2_3_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(3,callback(linear_form_3_0_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(3,callback(linear_form_3_1_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(3,callback(linear_form_3_2_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(3,callback(linear_form_3_3_first_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  // Second flux.
  
  wf.add_vector_form(0,callback(linear_form_0_2),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho_v_y));
  wf.add_vector_form(1,callback(linear_form_1_0_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(1,callback(linear_form_1_1_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(1,callback(linear_form_1_2_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(1,callback(linear_form_1_3_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(2,callback(linear_form_2_0_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(2,callback(linear_form_2_1_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(2,callback(linear_form_2_2_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
  wf.add_vector_form(2,callback(linear_form_2_3_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(3,callback(linear_form_3_0_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(3,callback(linear_form_3_1_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(3,callback(linear_form_3_2_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form(3,callback(linear_form_3_3_second_flux),HERMES_ANY, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  
  // Volumetric linear forms coming from the time discretization.
  wf.add_vector_form(0,linear_form, linear_form_order, HERMES_ANY, &prev_rho);
  wf.add_vector_form(1,linear_form, linear_form_order, HERMES_ANY, &prev_rho_v_x);
  wf.add_vector_form(2,linear_form, linear_form_order, HERMES_ANY, &prev_rho_v_y);
  wf.add_vector_form(3,linear_form, linear_form_order, HERMES_ANY, &prev_e);

  // Surface linear forms - inner edges coming from the DG formulation.
  wf.add_vector_form_surf(0,linear_form_interface_0, linear_form_order, H2D_DG_INNER_EDGE, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(1,linear_form_interface_1, linear_form_order, H2D_DG_INNER_EDGE, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(2,linear_form_interface_2, linear_form_order, H2D_DG_INNER_EDGE, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(3,linear_form_interface_3, linear_form_order, H2D_DG_INNER_EDGE, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));

  // Surface linear forms - inlet / outlet edges.
  wf.add_vector_form_surf(0,bdy_flux_inlet_outlet_comp_0, linear_form_order, 2, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(1,bdy_flux_inlet_outlet_comp_1, linear_form_order, 2, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(2,bdy_flux_inlet_outlet_comp_2, linear_form_order, 2, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(3,bdy_flux_inlet_outlet_comp_3, linear_form_order, 2, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));

  // Surface linear forms - Solid wall edges.
  wf.add_vector_form_surf(0,bdy_flux_solid_wall_comp_0, linear_form_order, 1, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(1,bdy_flux_solid_wall_comp_1, linear_form_order, 1, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(2,bdy_flux_solid_wall_comp_2, linear_form_order, 1, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(3,bdy_flux_solid_wall_comp_3, linear_form_order, 1, Tuple<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));

  // Initialize the FE problem.
  bool is_linear = true;
  DiscreteProblem dp(&wf, Tuple<Space*>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e), is_linear);

  // Filters for visualization of pressure and the two components of velocity.
  SimpleFilter pressure(calc_pressure_func, Tuple<MeshFunction*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));
  SimpleFilter u(calc_u_func, Tuple<MeshFunction*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));
  SimpleFilter w(calc_w_func, Tuple<MeshFunction*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));

  //VectorView vview("Velocity", 0, 0, 600, 300);
  //ScalarView sview("Pressure", 700, 0, 600, 300);

  ScalarView s1("w1", 0, 0, 600, 300);
  ScalarView s2("w2", 650, 0, 600, 300);
  ScalarView s3("w3", 0, 350, 600, 300);
  ScalarView s4("w4", 650, 350, 600, 300);

  // Iteration number.
  int iteration = 0;
  
  // Set up the solver, matrix, and rhs according to the solver selection.
  SparseMatrix* matrix = create_matrix(matrix_solver);
  Vector* rhs = create_vector(matrix_solver);
  Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);
  
  for(t = 0.0; t < TAU * 1000; t += TAU)
  {
    info("---- Time step %d, time %3.5f.", iteration, t);

    iteration++;

    // Assemble stiffness matrix and rhs or just rhs.
    bool rhs_only = iteration == 1 ? false : true;
    if (rhs_only == false) info("Assembling the stiffness matrix and right-hand side vector.");
    else info("Assembling the right-hand side vector (only).");
    dp.assemble(matrix, rhs, rhs_only);

        
    // Solve the matrix problem.
    info("Solving the matrix problem.");
    if(solver->solve())
      Solution::vector_to_solutions(solver->get_solution(), Tuple<Space *>(&space_rho, &space_rho_v_x, 
      &space_rho_v_y, &space_e), Tuple<Solution *>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));
    else
    error ("Matrix solver failed.\n");

    // Debugging.
    /*
    std::ofstream out("matrix");
    for(int i = 0; i < matrix->get_size(); i++)
      for(int j = 0; j < matrix->get_size(); j++)
        if(std::abs(matrix->get(i,j)) != 0)
          out << '(' << i << ',' << j << ')' << ':' << matrix->get(i,j) << std::endl;
    out.close();

    out.open("rhs");
      for(int j = 0; j < matrix->get_size(); j++)
        if(std::abs(rhs->get(j)) != 0)
          out << '(' << j << ')' << ':' << rhs->get(j) << std::endl;
    out.close();
     
    out.open("sol");
      for(int j = 0; j < matrix->get_size(); j++)
        out << '(' << j << ')' << ':' << solver->get_solution()[j] << std::endl;
    out.close();
    */    

    // Copy the solutions into the previous time level ones.
    prev_rho.copy(&sln_rho);
    prev_rho_v_x.copy(&sln_rho_v_x);
    prev_rho_v_y.copy(&sln_rho_v_y);
    prev_e.copy(&sln_e);

    // Visualization.
    /*
    pressure.reinit();
    u.reinit();
    w.reinit();
    sview.show(&pressure);
    vview.show(&u,&w);
    */

    s1.show(&sln_rho);
    s2.show(&sln_rho_v_x);
    s3.show(&sln_rho_v_y);
    s4.show(&sln_e);
  }
  return 0;
}