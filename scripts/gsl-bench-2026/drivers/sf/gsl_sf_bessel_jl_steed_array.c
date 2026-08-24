#include <gsl/gsl_sf.h>
#include <klee/klee.h>

double J[100];
int main() {
  int a;
  double b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  gsl_sf_result result;
  int status = gsl_sf_bessel_jl_steed_array(a,b,J);
  return 0;
}



