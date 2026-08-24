#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  int j,n;
  double q,x;

  klee_make_symbolic(&j, sizeof(j), "j");
  klee_make_symbolic(&n, sizeof(n), "n");
  klee_make_symbolic(&q, sizeof(q), "q");
  klee_make_symbolic(&x, sizeof(x), "x");

  gsl_sf_result result;
  int status = gsl_sf_mathieu_Ms_e(j,n,q,x,&result);
  return 0;
}



