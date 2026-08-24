#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b,c,d;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
  klee_make_symbolic(&d, sizeof(d), "d");

  gsl_sf_result_e10 result_e10;
  int status = gsl_sf_exp_mult_err_e10_e(a,b,c,d,&result_e10);
  return 0;
}



