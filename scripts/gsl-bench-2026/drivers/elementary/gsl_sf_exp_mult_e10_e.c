#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  gsl_sf_result_e10 result_e10;
  int status = gsl_sf_exp_mult_e10_e(a,b,&result_e10);
  return 0;
}



