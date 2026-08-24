#include <gsl/gsl_sf.h>
#include <klee/klee.h>

double J[100];
int main() {
  int a,b;
  double c;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");

  int status = gsl_sf_bessel_Yn_array(a,b,c,J);
  return 0;
}



