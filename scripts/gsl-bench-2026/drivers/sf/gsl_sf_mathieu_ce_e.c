#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  int n;
  double a,z;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&n, sizeof(n), "n");
  klee_make_symbolic(&z, sizeof(z), "z");

  gsl_sf_result result;
  int status = gsl_sf_mathieu_ce_e(n,a,z,&result);
  return 0;
}



