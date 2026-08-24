

//#include <stdio.h>
#include <gsl/gsl_math.h>
#include <klee/klee.h>

int main() {
  double x;
  int e=2;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_make_symbolic(&e, sizeof(e), "e");

  double result = gsl_ldexp(x,e);
  //printf("%lf\n",result);
  return 0;
}



