// add by yx

#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
  double Q=1;

  klee_make_symbolic(&Q,sizeof(Q),"Q");

  double result = gsl_cdf_ugaussian_Qinv(Q);
//  printf("%lf\n", result);
}