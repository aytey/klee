#include <gsl/gsl_sf.h>
#include <klee/klee.h>

double res_array[100];
int main() {
  int a;
  double b,c;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");

  int status = gsl_sf_legendre_H3d_array(a,b,c,res_array);
  return 0;
}



