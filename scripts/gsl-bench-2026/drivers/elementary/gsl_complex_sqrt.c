

//#include <stdio.h>
#include <gsl/gsl_complex_math.h>
#include <klee/klee.h>

int main() {
  double x,y;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_make_symbolic(&y, sizeof(y), "y");

  gsl_complex z = gsl_complex_rect(x, y);

  gsl_complex result = gsl_complex_sqrt(z);
  //printf("%lf\n",result);
  return 0;
}



