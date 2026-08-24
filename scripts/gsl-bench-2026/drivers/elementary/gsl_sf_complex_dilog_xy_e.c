#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  gsl_sf_result result_re,result_im;
  int status = gsl_sf_complex_dilog_xy_e(a,b,&result_re,&result_im);
  return 0;
}



