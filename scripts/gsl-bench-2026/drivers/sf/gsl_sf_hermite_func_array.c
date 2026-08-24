#include <gsl/gsl_sf.h>
#include <klee/klee.h>

double res_array[100];
int main() {
  int a;
  double b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  int status = gsl_sf_hermite_func_array(a,b,res_array);
  return 0;
}



