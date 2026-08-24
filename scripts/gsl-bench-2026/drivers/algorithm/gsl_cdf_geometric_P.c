// add by yx

#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
  int x=1;
  double mu=1;

  klee_make_symbolic(&x,sizeof(x),"x");
  klee_make_symbolic(&mu,sizeof(mu),"mu");

  double result = gsl_cdf_exponential_P(x, mu);
//  printf("%lf\n", result);
}