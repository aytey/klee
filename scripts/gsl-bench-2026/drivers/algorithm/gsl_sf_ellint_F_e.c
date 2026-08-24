#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b;
  int c;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");

  gsl_sf_result result;
  int status = gsl_sf_ellint_F_e(a,b,c,&result);
  return 0;
}



