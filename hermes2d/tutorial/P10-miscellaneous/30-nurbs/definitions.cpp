#include "hermes2d.h"

using namespace WeakFormsH1;

/* Weak forms */

class WeakFormPoisson : public WeakForm
{
public:
  WeakFormPoisson(double const_f) : WeakForm(1) {
    // Jacobian.
    add_matrix_form(new DefaultJacobianDiffusion(0, 0));

    // Residual.
    add_vector_form(new DefaultResidualDiffusion(0));
    add_vector_form(new DefaultVectorFormVol(0, HERMES_ANY, -const_f));
  };
};

