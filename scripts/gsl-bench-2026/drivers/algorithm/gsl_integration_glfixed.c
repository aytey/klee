/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <math.h>
#include <gsl/gsl_integration.h>

double f (double x, void * params) {
  double alpha = *(double *) params;
  double f = log(alpha*x) / sqrt(x);
  return f;
}

int
main (void)
{
  gsl_integration_glfixed_table * t = gsl_integration_glfixed_table_alloc (1000);

  double alpha = 1.0;
  double a=0,b=1;

  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  gsl_function F;
  F.function = &f;
  F.params = &alpha;

  double result = gsl_integration_glfixed(&F, a, b, t);

//  printf ("result          = % .18f\n", result);

  gsl_integration_glfixed_table_free (t);

  return 0;
}
