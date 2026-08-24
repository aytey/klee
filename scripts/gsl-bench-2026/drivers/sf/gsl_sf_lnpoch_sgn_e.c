#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b,sgn;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  gsl_sf_result result;
  int status = gsl_sf_lnpoch_sgn_e(a,b,&result,&sgn);
  return 0;
}



