

//#include <stdio.h>
#include <gsl/gsl_math.h>
#include <klee/klee.h>

int main() {
  double a;
  double b;
  double c;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
  double result = gsl_hypot3(a,b,c);
  //printf("%lf\n",result);
  return 0;
}



