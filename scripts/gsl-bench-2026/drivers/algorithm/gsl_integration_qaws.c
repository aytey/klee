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
  double a=0,b=1,epsabs=0,epsrel=1e-7;
  size_t limit = 1000;
  double alpha1=1,beta=1;
  int mu=0,nu=0;

  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&epsabs, sizeof(epsabs), "epsabs");
  klee_make_symbolic(&epsrel, sizeof(epsrel), "epsrel");
  klee_make_symbolic(&limit, sizeof(limit), "limit");
  klee_make_symbolic(&alpha1, sizeof(alpha1), "alpha1");
  klee_make_symbolic(&beta, sizeof(beta), "beta");
  klee_make_symbolic(&mu, sizeof(mu), "mu");
  klee_make_symbolic(&nu, sizeof(nu), "nu");

  gsl_integration_qaws_table * t = gsl_integration_qaws_table_alloc (alpha1, beta, mu, nu);

  gsl_function F;
  F.function = &f;
  F.params = &alpha;

  gsl_integration_qaws(&F, a, b, t, epsabs, epsrel, limit, w, &result, &error);

//  printf ("result          = % .18f\n", result);
//  printf ("exact result    = % .18f\n", expected);
//  printf ("estimated error = % .18f\n", error);
//  printf ("actual error    = % .18f\n", result - expected);
//  printf ("intervals       = %zu\n", w->size);

  gsl_integration_workspace_free (w);
  gsl_integration_qaws_table_free(t);

  return 0;
}
