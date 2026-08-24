

//#include <stdio.h>
#include <gsl/gsl_poly.h>
#include <klee/klee.h>

int main() {
  double a,b,c;
  
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
	
  double x0;
  double x1;
  gsl_poly_solve_quadratic(a,b,c,&x0,&x1);
  //printf("%lf %lf\n",x0,x1);
  return 0;
}



