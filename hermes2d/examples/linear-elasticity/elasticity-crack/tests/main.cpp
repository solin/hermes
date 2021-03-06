#define HERMES_REPORT_WARN
#define HERMES_REPORT_INFO
#define HERMES_REPORT_VERBOSE
#define HERMES_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace RefinementSelectors;

// This test makes sure that example "crack" works correctly.

const int INIT_REF_NUM = 0;                       // Number of initial uniform mesh refinements.
const int P_INIT_U1 = 2;                          // Initial polynomial degree of all mesh elements (u-displacement).
const int P_INIT_U2 = 2;                          // Initial polynomial degree of all mesh elements (v-displacement).
const bool MULTI = true;                          // true = use multi-mesh, false = use single-mesh.
                                                  // Note: in the single mesh option, the meshes are
                                                  // forced to be geometrically the same but the
                                                  // polynomial degrees can still vary.
const double THRESHOLD_MULTI = 0.35;              // error threshold for element refinement (multi-mesh)
const double THRESHOLD_SINGLE = 0.7;              // error threshold for element refinement (single-mesh)
const int STRATEGY = 0;                           // Adaptive strategy:
                                                  // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                                  //   error is processed. If more elements have similar errors, refine
                                                  //   all to keep the mesh symmetric.
                                                  // STRATEGY = 1 ... refine all elements whose error is larger
                                                  //   than THRESHOLD times maximum element error.
                                                  // STRATEGY = 2 ... refine all elements whose error is larger
                                                  //   than THRESHOLD.
                                                  // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO;          // Predefined list of element refinement candidates. Possible values are
                                                  // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                                  // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                                  // See User Documentation for details.
const int MESH_REGULARITY = -1;                   // Maximum allowed level of hanging nodes:
                                                  // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                                  // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                                  // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                                  // Note that regular meshes are not supported, this is due to
                                                  // their notoriously bad performance.
const double CONV_EXP = 1.0;                      // Default value is 1.0. This parameter influences the selection of
                                                  // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 0.5;                      // Stopping criterion for adaptivity (rel. error tolerance between the
                                                  // reference mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;                      // Adaptivity process stops when the number of degrees of freedom grows.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
                                                  // SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.

// Problem parameters.
const double E  = 200e9;                          // Young modulus for steel: 200 GPa.
const double nu = 0.3;                            // Poisson ratio.
const double g1 = -9.81;                          // Gravitational acceleration.
const double rho = 8000.0;                        // Material density in kg / m^3. 

// Boundary markers.
const std::string BDY_LEFT = "1";

// Weak forms.
#include "weakform/sample_weak_forms.h"

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh u1_mesh, u2_mesh;
  H2DReader mloader;
  mloader.load("../crack.mesh", &u1_mesh);

  // Perform initial uniform mesh refinement.
  for (int i=0; i < INIT_REF_NUM; i++) u1_mesh.refine_all_elements();

  // Create initial mesh for the vertical displacement component.
  // This also initializes the multimesh hp-FEM.
  u2_mesh.copy(&u1_mesh);

  // Initialize boundary conditions
  DefaultEssentialBCConst zero_disp(BDY_LEFT, 0.0);
  EssentialBCs bcs(&zero_disp);

  // Initialize the weak formulation.
  WeakFormLinearElasticity wf(E, nu, g1*rho);

  // Create H1 spaces with default shapeset for both displacement components.
  H1Space u1_space(&u1_mesh, &bcs, P_INIT_U1);
  H1Space u2_space(&u2_mesh, &bcs, P_INIT_U2);

  // Initialize coarse and reference mesh solutions.
  Solution u1_sln, u2_sln, u1_ref_sln, u2_ref_sln;

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // DOF and CPU convergence graphs.
  SimpleGraph graph_dof_est, graph_cpu_est;
  
  // Time measurement.
  TimePeriod cpu_time;
  cpu_time.tick();

  // Adaptivity loop:
  int as = 1; 
  bool done = false;
  do
  {
    info("---- Adaptivity step %d:", as);

    // Construct globally refined reference mesh and setup reference space.
    Hermes::vector<Space *>* ref_spaces = Space::construct_refined_spaces(Hermes::vector<Space *>(&u1_space, &u2_space));

    // Initialize matrix solver.
    SparseMatrix* matrix = create_matrix(matrix_solver);
    Vector* rhs = create_vector(matrix_solver);
    Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);

    // Assemble the reference problem.
    info("Solving on reference mesh.");
    bool is_linear = true;
    DiscreteProblem* dp = new DiscreteProblem(&wf, *ref_spaces, is_linear);
    dp->assemble(matrix, rhs);

    // Time measurement.
    cpu_time.tick();
    
    // Solve the linear system of the reference problem. If successful, obtain the solutions.
    if(solver->solve()) Solution::vector_to_solutions(solver->get_solution(), *ref_spaces, 
                                                      Hermes::vector<Solution *>(&u1_ref_sln, &u2_ref_sln));
    else error ("Matrix solver failed.\n");
  
    // Time measurement.
    cpu_time.tick();

    // Project the fine mesh solution onto the coarse mesh.
    info("Projecting reference solution on coarse mesh.");
    OGProjection::project_global(Hermes::vector<Space *>(&u1_space, &u2_space), Hermes::vector<Solution *>(&u1_ref_sln, &u2_ref_sln), 
                   Hermes::vector<Solution *>(&u1_sln, &u2_sln), matrix_solver); 

    // Initialize adaptivity.
    Adapt* adaptivity = new Adapt(Hermes::vector<Space *>(&u1_space, &u2_space));

    /* FIXME - this needs to be implemented.
    adaptivity->set_error_form(0, 0, bilinear_form_0_0<scalar, scalar>, bilinear_form_0_0<Ord, Ord>);
    adaptivity->set_error_form(0, 1, bilinear_form_0_1<scalar, scalar>, bilinear_form_0_1<Ord, Ord>);
    adaptivity->set_error_form(1, 0, bilinear_form_1_0<scalar, scalar>, bilinear_form_1_0<Ord, Ord>);
    adaptivity->set_error_form(1, 1, bilinear_form_1_1<scalar, scalar>, bilinear_form_1_1<Ord, Ord>);
    */
      
    // Calculate error estimate for each solution component and the total error estimate.
    info("Calculating error estimate and exact error."); 
    Hermes::vector<double> err_est_rel;
    double err_est_rel_total = adaptivity->calc_err_est(Hermes::vector<Solution *>(&u1_sln, &u2_sln), 
                               Hermes::vector<Solution *>(&u1_ref_sln, &u2_ref_sln), 
                               &err_est_rel) * 100;
    // Time measurement.
    cpu_time.tick();

    // Report results.
    info("ndof_coarse[0]: %d, ndof_fine[0]: %d, err_est_rel[0]: %g%%", 
         u1_space.Space::get_num_dofs(), (*ref_spaces)[0]->Space::get_num_dofs(), err_est_rel[0]*100);
    info("ndof_coarse[1]: %d, ndof_fine[1]: %d, err_est_rel[1]: %g%%",
         u2_space.Space::get_num_dofs(), (*ref_spaces)[1]->Space::get_num_dofs(), err_est_rel[1]*100);
    info("ndof_coarse_total: %d, ndof_fine_total: %d, err_est_rel_total: %g%%",
         Space::get_num_dofs(Hermes::vector<Space *>(&u1_space, &u2_space)), Space::get_num_dofs(*ref_spaces), err_est_rel_total);

    // Add entry to DOF and CPU convergence graphs.
    graph_dof_est.add_values(Space::get_num_dofs(Hermes::vector<Space *>(&u1_space, &u2_space)), err_est_rel_total);
    graph_dof_est.save("conv_dof_est.dat");
    graph_cpu_est.add_values(cpu_time.accumulated(), err_est_rel_total);
    graph_cpu_est.save("conv_cpu_est.dat");

    // If err_est too large, adapt the mesh.
    if (err_est_rel_total < ERR_STOP) 
      done = true;
    else 
    {
      info("Adapting coarse mesh.");
      done = adaptivity->adapt(Hermes::vector<RefinementSelectors::Selector *>(&selector, &selector), 
                               MULTI ? THRESHOLD_MULTI:THRESHOLD_SINGLE, STRATEGY, MESH_REGULARITY);
    }
    if (Space::get_num_dofs(Hermes::vector<Space *>(&u1_space, &u2_space)) >= NDOF_STOP) done = true;

    // Clean up.
    delete solver;
    delete matrix;
    delete rhs;
    delete adaptivity;
    if(done == false)
      for(unsigned int i = 0; i < ref_spaces->size(); i++)
        delete (*ref_spaces)[i]->get_mesh();
    delete ref_spaces;
    delete dp;
    
    // Increase counter.
    as++;
  }
  while (done == false);

  verbose("Total running time: %g s", cpu_time.accumulated());

  int ndof = Space::get_num_dofs(Hermes::vector<Space *>(&u1_space, &u2_space));

  int ndof_allowed = 600;
  printf("ndof actual = %d\n", ndof);
  printf("ndof allowed = %d\n", ndof_allowed);
  if (ndof <= ndof_allowed) {      // ndofs was 908 at the time this test was created
    printf("Success!\n");
    return ERR_SUCCESS;
  }
  else {
    printf("Failure!\n");
    return ERR_FAILURE;
  }
}
