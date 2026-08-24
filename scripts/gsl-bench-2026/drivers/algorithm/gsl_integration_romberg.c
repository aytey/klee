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
  gsl_integration_romberg_workspace * w = gsl_integration_romberg_alloc (1000);

  double result;
  double expected = -4.0;
  double alpha = 1.0;
  double a=0,b=1,epsabs=0,epsrel=1e-7;
  size_t nevals = 1000;

  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&epsabs, sizeof(epsabs), "epsabs");
  klee_make_symbolic(&epsrel, sizeof(epsrel), "epsrel");

  gsl_function F;
  F.function = &f;
  F.params = &alpha;

  gsl_integration_romberg (&F, a, b, epsabs, epsrel, &result, &nevals, w);

//  printf ("result          = % .18f\n", result);
//  printf ("exact result    = % .18f\n", expected);
//  printf ("actual error    = % .18f\n", result - expected);

  gsl_integration_romberg_free (w);

  return 0;
}
