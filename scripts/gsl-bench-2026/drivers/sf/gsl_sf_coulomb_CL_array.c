#include <gsl/gsl_sf.h>
#include <klee/klee.h>

double cl[100];
int main() {
  double a,c;
  int b;
  
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");

  int status = gsl_sf_coulomb_CL_array(a,b,c,cl);
  return 0;
}



