#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  unsigned a;
  klee_make_symbolic(&a, sizeof(a), "a");

  gsl_sf_result result;
  int status = gsl_sf_airy_zero_Bi_deriv_e(a,&result);
  return 0;
}



