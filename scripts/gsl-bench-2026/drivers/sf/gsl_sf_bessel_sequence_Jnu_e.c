#include <gsl/gsl_sf.h>
#include <klee/klee.h>

double J[100];
int main() {
  double a;
  int b,c;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");

  int status = gsl_sf_bessel_sequence_Jnu_e(a,b,c,J);
  return 0;
}



