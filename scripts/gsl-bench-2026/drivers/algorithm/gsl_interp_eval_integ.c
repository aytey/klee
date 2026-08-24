/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <gsl/gsl_interp.h>

int
main (void)
{

  double x0=1,x1=2,x2=3,y0=2,y1=4,y2=6, a=1,b=3;

  klee_make_symbolic(&x0, sizeof(x0), "x0");
  klee_make_symbolic(&x1, sizeof(x1), "x1");
  klee_make_symbolic(&x2, sizeof(x2), "x2");
  klee_make_symbolic(&y0, sizeof(y0), "y0");
  klee_make_symbolic(&y1, sizeof(y1), "y1");
  klee_make_symbolic(&y2, sizeof(y2), "y2");
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  double x[3] = {x0, x1, x2};
  double y[3] = {y0, y1, y2};

  gsl_interp_accel *acc = gsl_interp_accel_alloc();
  gsl_interp *interp = gsl_interp_alloc(gsl_interp_cspline, 3);
  gsl_interp_init(interp, x, y, 3);

//  printf("%lf %lf\n",interp->xmin, interp->xmax);

  double res = gsl_interp_eval_integ(interp, x, y, a, b, acc);

//  printf("%lf\n",res);

  gsl_interp_free (interp);
  gsl_interp_accel_free(acc);
  return 0;
}