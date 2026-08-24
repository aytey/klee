#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a;
  unsigned int b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  gsl_sf_result result;
  int status = gsl_sf_bessel_zero_Jnu_e(a,b,&result);
  return 0;
}



