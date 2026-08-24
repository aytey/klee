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
  gsl_integration_workspace * w = gsl_integration_workspace_alloc (1000);

  double result, error;
  double expected = -4.0;
  double alpha = 1.0;
  double a=0,b=1,c=5,epsabs=0,epsrel=1e-7;
  size_t limit = 1000;

  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
  klee_make_symbolic(&epsabs, sizeof(epsabs), "epsabs");
  klee_make_symbolic(&epsrel, sizeof(epsrel), "epsrel");
  klee_make_symbolic(&limit, sizeof(limit), "limit");

  gsl_function F;
  F.function = &f;
  F.params = &alpha;

  gsl_integration_qawc(&F, a, b, c, epsabs, epsrel, limit, w, &result, &error);

//  printf ("result          = % .18f\n", result);
//  printf ("exact result    = % .18f\n", expected);
//  printf ("estimated error = % .18f\n", error);
//  printf ("actual error    = % .18f\n", result - expected);
//  printf ("intervals       = %zu\n", w->size);

  gsl_integration_workspace_free (w);

  return 0;
}
