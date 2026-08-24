

#include <stdio.h>
#include <gsl/gsl_poly.h>
#include <gsl/gsl_complex_math.h>
#include <klee/klee.h>

int main() {

  double x1,y1,x2,y2,x3,y3;
  double a=1,b=1,c=1;

  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");

  gsl_complex z0 = gsl_complex_rect(x1, y1);
  gsl_complex z1 = gsl_complex_rect(x2, y2);
  gsl_complex z2 = gsl_complex_rect(x3, y3);

  double result = gsl_poly_complex_solve_cubic(a,b,c, &z0, &z1, &z2);
  //printf("%lf\n",result);
  return 0;
}



