#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  int n;
  double a;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&n, sizeof(n), "n");

  gsl_sf_result result;
  int status = gsl_sf_mathieu_a_e(n,a,&result);
  return 0;
}



