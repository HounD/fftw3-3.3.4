
sources = bluestein.c buffered.c conf.c ct.c dftw-direct.c	\
dftw-directsq.c dftw-generic.c dftw-genericbuf.c direct.c generic.c	\
indirect.c indirect-transpose.c kdft-dif.c kdft-difsq.c kdft-dit.c	\
kdft.c nop.c plan.c problem.c rader.c rank-geq2.c solve.c vrank-geq1.c	\
zero.c

LOCAL_SRC_FILES += $(sources:%=../dft/%)