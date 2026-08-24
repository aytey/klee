#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,sgn;
  klee_make_symbolic(&a, sizeof(a), "a");

  gsl_sf_result result;
  int status = gsl_sf_lngamma_sgn_e(a,&result,&sgn);
  return 0;
}



