#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a;
  klee_make_symbolic(&a, sizeof(a), "a");

  gsl_sf_result_e10 result_e10;
  int status = gsl_sf_exp_e10_e(a,&result_e10);
  return 0;
}



