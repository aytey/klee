/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_deriv.h>

double f (double x, void * params)
{
  (void)(params); /* avoid unused parameter warning */
  return pow (x, 1.5);
}

int
main (void)
{
  gsl_function F;
  double result, abserr;

  F.function = &f;
  F.params = 0;

//  printf ("f(x) = x^(3/2)\n");

  double x=2,h=1e-8;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_make_symbolic(&h, sizeof(h), "h");

  gsl_deriv_forward (&F, x, h, &result, &abserr);
//  printf ("x = 0.0\n");
//  printf ("f'(x) = %.10f +/- %.10f\n", result, abserr);
//  printf ("exact = %.10f\n", 0.0);

  return 0;
}