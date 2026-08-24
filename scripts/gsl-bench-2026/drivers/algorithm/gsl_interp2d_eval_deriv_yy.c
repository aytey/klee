/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <stdlib.h>

#include <gsl/gsl_math.h>
#include <gsl/gsl_interp2d.h>
#include <gsl/gsl_spline2d.h>

int
main()
{
  const gsl_interp2d_type *T = gsl_interp2d_bilinear;
//  const size_t N = 100;             /* number of points to interpolate */
  double x0=0, x1=1, y0=0, y1=1;
  double z0=0,z1=1,z2=0.5,z3=1;
  double x=0,y=1;

  klee_make_symbolic(&x0, sizeof(x0), "x0");
  klee_make_symbolic(&x1, sizeof(x1), "x1");
  klee_make_symbolic(&y0, sizeof(y0), "y0");
  klee_make_symbolic(&z0, sizeof(z0), "z0");
  klee_make_symbolic(&z1, sizeof(z1), "z1");
  klee_make_symbolic(&z2, sizeof(z2), "z2");
  klee_make_symbolic(&z3, sizeof(z3), "z3");
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_make_symbolic(&y, sizeof(y), "y");

  const double xa[] = { x0, x1 }; /* define unit square */
  const double ya[] = { y0, y1 };
  const size_t nx = sizeof(xa) / sizeof(double); /* x grid points */
  const size_t ny = sizeof(ya) / sizeof(double); /* y grid points */
  double *za = malloc(nx * ny * sizeof(double));
  gsl_interp2d *interp = gsl_interp2d_alloc(T, nx, ny);
  gsl_interp_accel *xacc = gsl_interp_accel_alloc();
  gsl_interp_accel *yacc = gsl_interp_accel_alloc();
//  size_t i, j;

  /* set z grid values */
  gsl_interp2d_set(interp, za, 0, 0, z0);
  gsl_interp2d_set(interp, za, 0, 1, z1);
  gsl_interp2d_set(interp, za, 1, 1, z2);
  gsl_interp2d_set(interp, za, 1, 0, z3);

  /* initialize interpolation */
  gsl_interp2d_init(interp, xa, ya, za, nx, ny);

  double zij = gsl_interp2d_eval_deriv_yy(interp, xa, ya, za, x, y, xacc, yacc);

//  printf("%f %f %f\n", x, y, zij);

  gsl_interp2d_free(interp);
  gsl_interp_accel_free(xacc);
  gsl_interp_accel_free(yacc);
  free(za);

  return 0;
}