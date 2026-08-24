/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <gsl/gsl_multifit.h>

int main ()
{
  int n;
  double chisq;
  gsl_matrix *X, *cov;
  gsl_vector *y, *w, *c;
  n = 3;

  X = gsl_matrix_alloc (3, 3);
  y = gsl_vector_alloc (n);
  w = gsl_vector_alloc (n);

  c = gsl_vector_alloc (3);
  cov = gsl_matrix_alloc (3, 3);

  double x0=1, x1=2, x2=4, y0=1, y1=1, y2=2, e0=0, e1=2, e2=3;

  klee_make_symbolic(&x0, sizeof(x0), "x0");
  klee_make_symbolic(&x1, sizeof(x1), "x1");
  klee_make_symbolic(&x2, sizeof(x2), "x2");
  klee_make_symbolic(&y0, sizeof(y0), "y0");
  klee_make_symbolic(&y1, sizeof(y1), "y1");
  klee_make_symbolic(&y2, sizeof(y2), "y2");
  klee_make_symbolic(&e0, sizeof(e0), "e0");
  klee_make_symbolic(&e1, sizeof(e1), "e1");
  klee_make_symbolic(&e2, sizeof(e2), "e2");

  gsl_matrix_set (X, 0, 0, 1.0);
  gsl_matrix_set (X, 0, 1, x0);
  gsl_matrix_set (X, 0, 2, x0*x0);

  gsl_matrix_set (X, 0, 0, 1.0);
  gsl_matrix_set (X, 0, 1, x1);
  gsl_matrix_set (X, 0, 2, x1*x1);

  gsl_matrix_set (X, 1, 0, 1.0);
  gsl_matrix_set (X, 1, 1, x2);
  gsl_matrix_set (X, 1, 2, x2*x2);

  gsl_vector_set (y, 0, y0);
  gsl_vector_set (w, 0, 1.0/(e0*e0));
  gsl_vector_set (y, 0, y1);
  gsl_vector_set (w, 0, 1.0/(e1*e1));
  gsl_vector_set (y, 1, y2);
  gsl_vector_set (w, 1, 1.0/(e2*e2));

  {
    gsl_multifit_linear_workspace * work
            = gsl_multifit_linear_alloc (n, 3);
    gsl_multifit_wlinear (X, w, y, c, cov,
                          &chisq, work);
    gsl_multifit_linear_free (work);
  }

  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_vector_free (c);
  gsl_matrix_free (cov);

  return 0;
}