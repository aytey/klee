// add by yx

#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
  double x=1, a=1;

  klee_make_symbolic(&x,sizeof(x),"x");
  klee_make_symbolic(&a,sizeof(a),"a");

  double result = gsl_cdf_cauchy_Pinv(x, a);
//  printf("%lf\n", result);
}