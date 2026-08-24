// add by yx

#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
  double x=1, nu1=1, nu2=1;

  klee_make_symbolic(&x,sizeof(x),"x");
  klee_make_symbolic(&nu1,sizeof(nu1),"nu1");
  klee_make_symbolic(&nu2,sizeof(nu2),"nu2");

  double result = gsl_cdf_beta_Q(x, nu1, nu2);
//  printf("%lf\n", result);
}