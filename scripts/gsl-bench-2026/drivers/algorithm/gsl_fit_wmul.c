/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <gsl/gsl_fit.h>

int
main (void)
{
  int n = 4;

  double w0=0.1, w1=0.2, w2=0.3, w3=0.4;

  klee_make_symbolic(&w0, sizeof(w0), "w0");
  klee_make_symbolic(&w1, sizeof(w1), "w1");
  klee_make_symbolic(&w2, sizeof(w2), "w2");
  klee_make_symbolic(&w3, sizeof(w3), "w3");

  double x[4] = { 1970, 1980, 1990, 2000 };
  double y[4] = {   12,   11,   14,   13 };
  double w[4] = {  w0,  w1,  w2,  w3 };

  double c1, cov11, sumsq;

  gsl_fit_wmul(x, 1, w, 1, y, 1, n, &c1, &cov11, &sumsq);

//  printf ("# chisq = %g\n", sumsq);

  return 0;
}