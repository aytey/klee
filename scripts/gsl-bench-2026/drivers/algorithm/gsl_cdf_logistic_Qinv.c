// add by yx

#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
  double x=1, mu=1;

  klee_make_symbolic(&x,sizeof(x),"x");
  klee_make_symbolic(&mu,sizeof(mu),"mu");

  double result = gsl_cdf_logistic_Qinv(x, mu);
//  printf("%lf\n", result);
}