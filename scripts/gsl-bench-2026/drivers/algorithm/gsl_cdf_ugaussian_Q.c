// add by yx

#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
  double x=1;

  klee_make_symbolic(&x,sizeof(x),"x");

  double result = gsl_cdf_ugaussian_Q (x);
//  printf("%lf\n", result);
}