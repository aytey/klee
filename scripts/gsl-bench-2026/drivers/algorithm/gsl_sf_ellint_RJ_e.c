#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b,c,d;
  int e;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
  klee_make_symbolic(&d, sizeof(d), "d");
  klee_make_symbolic(&e, sizeof(e), "e");

  gsl_sf_result result;
  int status = gsl_sf_ellint_RJ_e(a,b,c,d,e,&result);
  return 0;
}



