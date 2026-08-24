#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b,c,d;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
  klee_make_symbolic(&d, sizeof(d), "d");

  gsl_sf_result result;
  int status = gsl_sf_hyperg_2F1_conj_renorm_e(a,b,c,d,&result);
  return 0;
}



