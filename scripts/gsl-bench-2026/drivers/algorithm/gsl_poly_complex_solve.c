

//#include <stdio.h>
#include <gsl/gsl_poly.h>
#include <klee/klee.h>

int main (void)
{
  
  double a0,a1,a2,a3,a4,a5;
  klee_make_symbolic(&a0, sizeof(a0), "a0");
  klee_make_symbolic(&a1, sizeof(a1), "a1");
  klee_make_symbolic(&a2, sizeof(a2), "a2");
  klee_make_symbolic(&a3, sizeof(a3), "a3");
  klee_make_symbolic(&a4, sizeof(a4), "a4");
  klee_make_symbolic(&a5, sizeof(a5), "a5");
  double a[6] = {a0, a1, a2, a3, a4, a5};

  double z[10];

  gsl_poly_complex_workspace * w = gsl_poly_complex_workspace_alloc (6);

  int status = gsl_poly_complex_solve (a, 6, w, z);
  gsl_poly_complex_workspace_free (w);

//  int i;
//  for (i = 0; i < 5; i++)
//    {
//      printf ("z%d = %+.18f %+.18f\n",
//              i, z[2*i], z[2*i+1]);
//    }

  return 0;
}



