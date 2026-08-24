/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <math.h>
#include <gsl/gsl_integration.h>

double f (double x, void * params) {
  double alpha = *(double *) params;
//  double f = log(alpha*x) / sqrt(x);
  double f = alpha*x;
  return f;
}

int
main (void)
{
  gsl_integration_workspace * w = gsl_integration_workspace_alloc (1000);

  double result, error;
  double expected = -4.0;
  double alpha = 1.0;
  double a=0,epsabs=0,epsrel=1e-7;
  size_t limit = 1000;
  double omega=1,L=1;
  size_t n=100;

  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&epsabs, sizeof(epsabs), "epsabs");
  klee_make_symbolic(&epsrel, sizeof(epsrel), "epsrel");
  klee_make_symbolic(&limit, sizeof(limit), "limit");
  klee_make_symbolic(&omega, sizeof(omega), "omega");
  klee_make_symbolic(&L, sizeof(L), "L");
  klee_make_symbolic(&n, sizeof(n), "n");

  gsl_integration_qawo_table * wf = gsl_integration_qawo_table_alloc(omega, L, GSL_INTEG_SINE, n);

  gsl_function F;
  F.function = &f;
  F.params = &alpha;

  gsl_integration_qawo(&F, a, epsabs, epsrel, limit, w, wf, &result, &error);

//  printf ("result          = % .18f\n", result);
//  printf ("exact result    = % .18f\n", expected);
//  printf ("estimated error = % .18f\n", error);
//  printf ("actual error    = % .18f\n", result - expected);
//  printf ("intervals       = %zu\n", w->size);

  gsl_integration_workspace_free (w);
  gsl_integration_qawo_table_free(wf);

  return 0;
}
