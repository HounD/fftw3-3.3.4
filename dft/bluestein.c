/*
 * Copyright (c) 2003 Matteo Frigo
 * Copyright (c) 2003 Massachusetts Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "dft.h"

typedef struct {
     solver super;
} S;

typedef struct {
     plan_dft super;
     int n;     /* problem size */
     int nb;    /* size of convolution */
     R *w;      /* lambda k . exp(2*pi*i*k^2/(2*n)) */
     R *W;      /* DFT(w) */
     plan *cldf, *cldb;
     int is, os;
} P;

static void bluestein_sequence(int n, R *w)
{
     int k, ksq, n2 = 2 * n;

     ksq = 1; /* (-1)^2 */
     for (k = 0; k < n; ++k) {
          /* careful with overflow */
          ksq = ksq + 2*k - 1; while (ksq > n2) ksq -= n2;
          w[2*k] = X(cos2pi)(ksq, n2);
          w[2*k+1] = X(sin2pi)(ksq, n2);
     }
}

static void mktwiddle(P *p)
{
     int i;
     int n = p->n, nb = p->nb;
     R *w, *W;
     R nbinv = 1.0 / nb;  /* exact because nb = 2^k */

     p->w = w = MALLOC(2 * n * sizeof(R), TWIDDLES);
     p->W = W = MALLOC(2 * nb * sizeof(R), TWIDDLES);
     bluestein_sequence(n, w);

     for (i = 0; i < nb; ++i)
          W[2*i] = W[2*i+1] = 0;
     for (i = 0; i < n; ++i) {
          W[2*i] = w[2*i] * nbinv;
          W[2*i+1] = w[2*i+1] * nbinv;
     }
     for (i = 1; i < n; ++i) {
          W[2*(nb-i)] = w[2*i] * nbinv;
          W[2*(nb-i)+1] = w[2*i+1] * nbinv;
     }

     {
          plan_dft *cldf = (plan_dft *)p->cldf;
	  /* cldf must be awake */
          cldf->apply(p->cldf, W, W+1, W, W+1);
     }
}

static void apply(const plan *ego_, R *ri, R *ii, R *ro, R *io)
{
     const P *ego = (const P *) ego_;
     int i, n = ego->n, nb = ego->nb, is = ego->is, os = ego->os;
     R *w = ego->w, *W = ego->W;
     R *b = (R *) MALLOC(2 * nb * sizeof(R), BUFFERS);

     /* multiply input by conjugate bluestein sequence */
     for (i = 0; i < n; ++i) {
	  E xr = ri[i*is], xi = ii[i*is];
          E wr = w[2*i], wi = w[2*i+1];
          b[2*i] = xr * wr + xi * wi;
          b[2*i+1] = xi * wr - xr * wi;
     }

     for (; i < nb; ++i) b[2*i] = b[2*i+1] = 0;

     /* convolution: FFT */
     {
          plan_dft *cldf = (plan_dft *)ego->cldf;
          cldf->apply(ego->cldf, b, b+1, b, b+1);
     }

     /* convolution: pointwise multiplication */
     for (i = 0; i < nb; ++i) {
	  E xr = b[2*i], xi = b[2*i+1];
          E wr = W[2*i], wi = W[2*i+1];
          b[2*i] = xr * wr - xi * wi;
          b[2*i+1] = xi * wr + xr * wi;
     }

     /* convolution: IFFT */
     {
          plan_dft *cldb = (plan_dft *)ego->cldb;
          cldb->apply(ego->cldb, b+1, b, b+1, b);
     }

     /* multiply output by conjugate bluestein sequence */
     for (i = 0; i < n; ++i) {
	  E xr = b[2*i], xi = b[2*i+1];
          E wr = w[2*i], wi = w[2*i+1];
          ro[i*os] = xr * wr + xi * wi;
          io[i*os] = xi * wr - xr * wi;
     }

     X(ifree)(b);	  
}

static void awake(plan *ego_, int flg)
{
     P *ego = (P *) ego_;

     AWAKE(ego->cldf, flg);
     AWAKE(ego->cldb, flg);

     if (flg) {
	  A(!ego->w);
	  mktwiddle(ego);
     } else {
	  X(ifree0)(ego->w); ego->w = 0;
	  X(ifree0)(ego->W); ego->W = 0;
     }
}

static int applicable0(const problem *p_)
{
     if (DFTP(p_)) {
          const problem_dft *p = (const problem_dft *) p_;
          return (1
		  && p->sz->rnk == 1
		  && p->vecsz->rnk == 0
		  /* FIXME: allow other sizes */
		  && X(is_prime)(p->sz->dims[0].n)
	       );
     }

     return 0;
}

static int applicable(const solver *ego, const problem *p_, 
		      const planner *plnr)
{
     UNUSED(ego);
     if (NO_UGLYP(plnr)) return 0; /* always ugly */
     if (!applicable0(p_)) return 0;
     return 1;
}

static void destroy(plan *ego_)
{
     P *ego = (P *) ego_;
     X(plan_destroy_internal)(ego->cldf);
     X(plan_destroy_internal)(ego->cldb);
}

static void print(const plan *ego_, printer *p)
{
     const P *ego = (const P *)ego_;
     p->print(p, "(dft-bluestein-%d%(%p%)%(%p%))",
              ego->n, ego->cldf, ego->cldb);
}

static int pow2_atleast(int x)
{
     int h;
     for (h = 1; h < x; h = 2 * h)
	  ;
     return h;
}

static plan *mkplan(const solver *ego, const problem *p_, planner *plnr)
{
     const problem_dft *p = (const problem_dft *) p_;
     P *pln;
     int n, nb;
     plan *cldf = 0, *cldb = 0;
     R *buf = (R *) 0;

     static const plan_adt padt = {
	  X(dft_solve), awake, print, destroy
     };

     if (!applicable(ego, p_, plnr))
	  return (plan *) 0;

     n = p->sz->dims[0].n;
     nb = pow2_atleast(2 * n - 1);
     buf = (R *) MALLOC(2 * nb * sizeof(R), BUFFERS);

     cldf = X(mkplan_d)(plnr, 
			X(mkproblem_dft_d)(X(mktensor_1d)(nb, 2, 2),
					   X(mktensor_1d)(1, 0, 0),
  					   buf, buf+1, 
					   buf, buf+1));
     if (!cldf) goto nada;

     cldb = X(mkplan_d)(plnr, 
			X(mkproblem_dft_d)(X(mktensor_1d)(nb, 2, 2),
					   X(mktensor_1d)(1, 0, 0),
  					   buf+1, buf, 
					   buf+1, buf));
     if (!cldb) goto nada;

     X(ifree)(buf);

     pln = MKPLAN_DFT(P, &padt, apply);

     pln->n = n;
     pln->nb = nb;
     pln->w = 0;
     pln->W = 0;
     pln->cldf = cldf;
     pln->cldb = cldb;
     pln->is = p->sz->dims[0].is;
     pln->os = p->sz->dims[0].os;

     return &(pln->super.super);

 nada:
     X(ifree0)(buf);
     X(plan_destroy_internal)(cldf);
     X(plan_destroy_internal)(cldb);
     return (plan *)0;
}


static solver *mksolver(void)
{
     static const solver_adt sadt = { mkplan };
     S *slv = MKSOLVER(S, &sadt);
     return &(slv->super);
}

void X(dft_bluestein_register)(planner *p)
{
     REGISTER_SOLVER(p, mksolver());
}