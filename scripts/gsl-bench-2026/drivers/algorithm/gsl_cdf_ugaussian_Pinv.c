// add by yx

#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
  double P=1;

  klee_make_symbolic(&P,sizeof(P),"P");

  double result = gsl_cdf_ugaussian_Pinv(P);
//  printf("%lf\n", result);
}