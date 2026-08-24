// add by yx

#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
  double x=1, a=1, b=1;

  klee_make_symbolic(&x,sizeof(x),"x");
  klee_make_symbolic(&a,sizeof(a),"a");
  klee_make_symbolic(&b,sizeof(b),"b");

  double result = gsl_cdf_exppow_P(x, a, b);
//  printf("%lf\n", result);
}