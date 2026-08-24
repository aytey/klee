#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  gsl_sf_result result_a,result_b;
  int status = gsl_sf_complex_cos_e(a,b,&result_a,&result_b);
  return 0;
}



