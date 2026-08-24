

//#include <stdio.h>
#include <gsl/gsl_math.h>
#include <klee/klee.h>

int main() {
  double x;
  int n;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_make_symbolic(&n, sizeof(n), "n");

  double result = gsl_pow_int(x,n);
  //printf("%lf\n",result);
  return 0;
}



