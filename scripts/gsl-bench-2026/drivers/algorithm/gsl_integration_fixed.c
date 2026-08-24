/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <math.h>
#include <gsl/gsl_integration.h>
#include <gsl/gsl_sf_gamma.h>

double
f(double x, void * params)
{
  int m = *(int *) params;
  double f = gsl_pow_int(x, m) + 1.0;
  return f;
}

int main (int argc, char *argv[])
{
  gsl_integration_fixed_workspace * w;
  const gsl_integration_fixed_type * T = gsl_integration_fixed_hermite;
  int m = 10;
  int n = 6;
  double a=0,b=1,alpha=0,beta=0;
  double expected, result;
  gsl_function F;

  if (argc > 1)
    m = atoi(argv[1]);

  if (argc > 2)
    n = atoi(argv[2]);

  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&alpha, sizeof(alpha), "alpha");
  klee_make_symbolic(&beta, sizeof(beta), "beta");

  w = gsl_integration_fixed_alloc(T, n, a, b, alpha, beta);

  F.function = &f;
  F.params = &m;

  gsl_integration_fixed(&F, &result, w);

  if (m % 2 == 0)
    expected = M_SQRTPI + gsl_sf_gamma(0.5*(1.0 + m));
  else
    expected = M_SQRTPI;

//  printf ("m               = %d\n", m);
//  printf ("intervals       = %zu\n", gsl_integration_fixed_n(w));
//  printf ("result          = % .18f\n", result);
//  printf ("exact result    = % .18f\n", expected);
//  printf ("actual error    = % .18f\n", result - expected);

  gsl_integration_fixed_free (w);

  return 0;
}
