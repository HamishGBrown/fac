#include "crm.h"
#include "grid.h"

static char *rcsid="$Id: crm.c,v 1.16 2002/02/20 18:30:16 mfgu Exp $";
#if __GNUC__ == 2
#define USE(var) static void * use_##var = (&use_##var, (void *) &var) 
USE (rcsid);
#endif

static IONIZED ion0;
static ARRAY *ions;
static ARRAY *blocks;
static double *bmatrix = NULL;

static int n_single_blocks = 64;

static int rec_cascade = 1;
static double cas_accuracy = EPS4;
static int max_iter = 256;
static double iter_accuracy = EPS3;
static double iter_stablizer = 0.75;

static double electron_density = EPS3; /* 10^10 cm-3 */
static double photon_density = 0.0; /* erg/(eV cm3) */
static int ai_extra_nmax = 400;

void uvip3p_(int *np, int *ndp, double *x, double *y, 
	     int *n, double *xi, double *yi);
void dgesv_(int *n, int *nrhs, double *a, int *lda, int *ipvt,
	    double *b, int *ldb, int *info);


int SetNumSingleBlocks(int n) {
  n_single_blocks = n;
  return 0;
}

int SetEleDensity(double ele) {
  if (ele >= 0.0) electron_density = ele;
  return 0;
}

int SetPhoDensity(double pho) {
  if (pho >= 0.0) photon_density = pho;
  return 0;
}

int SetIteration(double acc, double s, int max) {
  if (max >= 0) max_iter = max;
  if (acc > 0) iter_accuracy = acc;
  if (s > 0.0 && s < 1.0) iter_stablizer = s;
  return 0;
}

int SetCascade(int c, double a) {
  rec_cascade = c;
  if (a > 0.0) cas_accuracy = a;
  return 0;
}

int InitCRM(void) {
  int i;

  for (i = 0; i < NDB; i++) ion0.dbfiles[i] = NULL;
  ion0.nionized = 0;
  ion0.energy = NULL;
  ion0.atom = 0;

  ions = (ARRAY *) malloc(sizeof(ARRAY));
  ArrayInit(ions, sizeof(ION), ION_BLOCK);
  blocks = (ARRAY *) malloc(sizeof(ARRAY));
  ArrayInit(blocks, sizeof(LBLOCK), LBLOCK_BLOCK);
  bmatrix = NULL;
  
  InitDBase();
  InitRates();

  return 0;
}

static void FreeBlkRateData(void *p) {
  BLK_RATE *r;

  r = (BLK_RATE *) p;
  ArrayFree(r->rates, NULL);
  if (r->rates) free(r->rates);
  r->rates = NULL;
}

static void FreeIonData(void *p) {
  ION *ion;
  int i;

  ion = (ION *) p;
  if (ion->nlevels > 0) {
    free(ion->iblock);
    free(ion->ilev);
    free(ion->j);
    free(ion->energy);
    ion->nlevels = 0;
  }
  for (i = 0; i < NDB; i++) {
    if (ion->dbfiles[i]) free(ion->dbfiles[i]);
    ion->dbfiles[i] = NULL;
  }
  ArrayFree(ion->ce_rates, FreeBlkRateData);
  free(ion->ce_rates);
  ion->ce_rates = NULL;
  ArrayFree(ion->tr_rates, FreeBlkRateData);
  free(ion->tr_rates);
  ion->tr_rates = NULL;
  ArrayFree(ion->ci_rates, FreeBlkRateData);
  free(ion->ci_rates);
  ion->ci_rates = NULL;
  ArrayFree(ion->rr_rates, FreeBlkRateData);
  free(ion->rr_rates);
  ion->rr_rates = NULL;
  ArrayFree(ion->ai_rates, FreeBlkRateData);
  free(ion->ai_rates);
  ion->ai_rates = NULL;
  ArrayFree(ion->recombined, NULL);
  free(ion->recombined);
  ion->recombined = NULL;
}

static void FreeBlockData(void *p) {
  LBLOCK *blk;

  blk = (LBLOCK *) p;
  if (blk->nlevels > 0) {
    free(blk->n);
    free(blk->n0);
    free(blk->r);
    free(blk->total_rate);
  }
  blk->nlevels = 0;
}

int ReinitCRM(int m) {
  ION *ion;
  int i, k;

  if (m < 0) return 0;

  ReinitDBase(0);
  if (m == 2) return 0;
  
  if (m == 1) {
    for (k = 0; k < ions->dim; k++) {
      ion = (ION *) ArrayGet(ions, k);
      ArrayFree(ion->ce_rates, FreeBlkRateData);
      ArrayFree(ion->tr_rates, FreeBlkRateData);
      ArrayFree(ion->ci_rates, FreeBlkRateData);
      ArrayFree(ion->rr_rates, FreeBlkRateData);
      ArrayFree(ion->ai_rates, FreeBlkRateData);
    }
    return 0;
  }
  for (i = 0; i < NDB; i++) {
    if (ion0.dbfiles[i]) free(ion0.dbfiles[i]);
    ion0.dbfiles[i] = NULL;
  }
  if (ion0.nionized > 0) {
    free(ion0.energy);
    free(ion0.ionized_map[0]);
    free(ion0.ionized_map[1]);
    ion0.nionized = 0;
  }
  ion0.atom = 0;
  ArrayFree(ions, FreeIonData);
  ArrayFree(blocks, FreeBlockData);
  if (bmatrix) free(bmatrix);
  bmatrix = NULL;
  
  return 0;
}

int AddIon(int nele, double n, char *pref) {
  ION ion;
  int i;
  int m;
  
  ion.nlevels = 0;
  ion.ce_rates = (ARRAY *) malloc(sizeof(ARRAY));
  ArrayInit(ion.ce_rates, sizeof(BLK_RATE), RATES_BLOCK);
  ion.tr_rates = (ARRAY *) malloc(sizeof(ARRAY));
  ArrayInit(ion.tr_rates, sizeof(BLK_RATE), RATES_BLOCK);
  ion.rr_rates = (ARRAY *) malloc(sizeof(ARRAY));
  ArrayInit(ion.rr_rates, sizeof(BLK_RATE), RATES_BLOCK);
  ion.ci_rates = (ARRAY *) malloc(sizeof(ARRAY));
  ArrayInit(ion.ci_rates, sizeof(BLK_RATE), RATES_BLOCK);
  ion.ai_rates = (ARRAY *) malloc(sizeof(ARRAY));
  ArrayInit(ion.ai_rates, sizeof(BLK_RATE), RATES_BLOCK);

  ion.recombined = (ARRAY *) malloc(sizeof(ARRAY));
  ArrayInit(ion.recombined, sizeof(RECOMBINED), 16);
  
  ion.nele = nele;
  m = strlen(pref);
  m = m+4;
  for (i = 0; i < NDB; i++) {
    ion.dbfiles[i] = malloc(m);
    switch (i+1) {
    case DB_EN:
      sprintf(ion.dbfiles[i], "%s.en", pref);
      break;
    case DB_TR:
      sprintf(ion.dbfiles[i], "%s.tr", pref);
      break;
    case DB_CE:
      sprintf(ion.dbfiles[i], "%s.ce", pref);
      break;      
    case DB_RR:
      sprintf(ion.dbfiles[i], "%s.rr", pref);
      break;
    case DB_AI:
      sprintf(ion.dbfiles[i], "%s.ai", pref);
      break;
    case DB_CI:
      sprintf(ion.dbfiles[i], "%s.ci", pref);
      break;
    default:
      break;
    }
  }

  ion.n = n;

  ArrayAppend(ions, &ion);
  
  return ions->dim;
  
}

void GetRecombined(int *b, int *nrec, char *name) {
  int i;
  
  *nrec = 0;
  i = 0;
  while (name[i] && name[i] != '+') i++;
  if (name[i] == '+' && name[i-1] == ' ') {
    *b = atoi(name);
    *nrec = atoi(&(name[i+1]));
  }
}

void ExtrapolateEN(int iion, ION *ion) {
  RECOMBINED *rec;
  LBLOCK blk, *blkp;
  int i, j, nr, nlev;
  int n, n0, n1, nr0;
  int t, p, q, s, k, nc;
  double a, b, c, d;

  nlev = ion->nlevels;
  for (i = 0; i < ion->recombined->dim; i++) {
    rec = (RECOMBINED *) ArrayGet(ion->recombined, i);    
    j = rec->n-1;
    if (j > 0) {
      n1 = rec->nrec[j];
      n0 = rec->nrec[j-1]+1;
      nr = rec->imax[j] - rec->imin[j] + 1;
      k = n1 - n0;
      nlev += nr*k;
    }
  }
  ion->iblock = (LBLOCK **) realloc(ion->iblock, sizeof(LBLOCK *)*nlev);
  ion->ilev = (int *) realloc(ion->ilev, sizeof(int)*nlev);
  ion->j = (short *) realloc(ion->j, sizeof(short)*nlev);
  ion->energy = (double *) realloc(ion->energy, sizeof(double)*nlev);
  
  nr0 = ion->nlevels;
  c = ion0.atom - ion->nele + 1.0;
  c = 0.5*c*c;
  for (i = 0; i < ion->recombined->dim; i++) {
    rec = (RECOMBINED *) ArrayGet(ion->recombined, i);
    j = rec->n-1;
    rec->n_ext = rec->n;
    if (j > 0) {
      CopyNComplex(blk.ncomplex, ion->iblock[rec->imin[j]]->ncomplex);
      nc = 0;
      while (blk.ncomplex[nc+1].n > 0) nc++;
      n1 = rec->nrec[j];
      n0 = rec->nrec[j-1]+1;
      nr = rec->imax[j] - rec->imin[j] + 1;
      a = -c/(n1*n1);
      for (n = n0, t = rec->n; n < n1; n++, t++) {
	rec->nrec[t] = n;
	rec->imin[t] = nr0;
	nr0 += nr;
	rec->imax[t] = nr0-1;
	blk.ib = blocks->dim;
	blk.iion = iion;
	blk.nlevels = nr;
	blk.n = (double *) malloc(sizeof(double)*nr);
	blk.n0 = (double *) malloc(sizeof(double)*nr);
	blk.r = (double *) malloc(sizeof(double)*nr);
	blk.total_rate = (double *) malloc(sizeof(double)*nr);
	blk.rec = rec;
	blk.irec = t;
	blk.ncomplex[nc].n = n;
	blkp = ArrayAppend(blocks, &blk);
	q = -1;
	p = rec->imin[t];
	s = rec->imin[j-1];
	d = -c/(n*n);
	for (k = rec->imin[j]; k <= rec->imax[j]; k++, p++, s++) {
	  q++;
	  ion->iblock[p] = blkp;
	  ion->ilev[p] = q;
	  ion->j[p] = ion->j[k];
	  if (s <= rec->imax[j-1]) {
	    b = ion->energy[s] - (ion->energy[k] - a);
	    b = -c/b;
	    b = b/((n0-1.0)*(n0-1.0));
	    b = 1.0+(b-1.0)*(n-n1)/(n0-1.0-n1);
	    ion->energy[p] = ion->energy[k] - a + d/b;
	  } else {
	    ion->energy[p] = ion->energy[k] - a + d;
	  }
	}
      }
      rec->n_ext = t;
    }
  }
  ion->nlevels = nlev;
}
  
void ExtrapolateTR(ION *ion, int inv) {
  RECOMBINED *rec;
  RATE *r, r0;
  ARRAY *rates;
  LBLOCK *blk, *blk0;
  BLK_RATE *brts;
  int nr;
  int n0, n1;
  int i, j, t, k, p, q, s;
  int imin, imax;
  double a, b;

  for (i = 0; i < ion->recombined->dim; i++) {
    rec = (RECOMBINED *) ArrayGet(ion->recombined, i);
    if (rec->n_ext == rec->n) continue;
    j = rec->n-1;
    imin = rec->imin[j];
    imax = rec->imax[j];
    blk0 = ion->iblock[imin];
    n1 = rec->nrec[j];
    a = (double) n1;
    a = a*a*a;
    for (p = 0; p < ion->tr_rates->dim; p++) {
      brts = (BLK_RATE *) ArrayGet(ion->tr_rates, p);
      if (brts->iblock != blk0) continue;
      blk = brts->fblock;
      rates = brts->rates;
      nr = rates->dim;
      for (t = 0; t < nr; t++) {
	r = (RATE *) ArrayGet(rates, t);
	q = -1;
	if (blk->rec) {
	  if (blk->rec->nrec[blk->irec] == n1) {
	    q = r->f - blk->rec->imin[blk->irec];
	  }
	}
	for (k = rec->n; k < rec->n_ext; k++) {
	  n0 = rec->nrec[k];
	  r0.i = r->i - imin + rec->imin[k];
	  if (q < 0) {
	    r0.f = r->f;
	    b = 1.0/((double) n0);
	    b = b*b*b*a;
	    r0.dir = b*r->dir;
	    r0.inv = b*r->inv;
	    AddRate(ion, ion->tr_rates, &r0, 0);
	  } else {
	    for (s = 0; s < blk->rec->n_ext; s++) {
	      if (blk->rec->nrec[s] == n0) {
		r0.f = q + blk->rec->imin[s];
		if (r0.f > blk->rec->imax[s]) break;
		r0.dir = r->dir;
		r0.inv = r->inv;
		AddRate(ion, ion->tr_rates, &r0, 0);
		break;
	      }
	    }
	  }
	}
      }
    }
  }
}

void ExtrapolateRR(ION *ion, int inv) {
  RECOMBINED *rec;
  RATE *r, r0;
  ARRAY *rates;
  LBLOCK *blk0;
  BLK_RATE *brts;
  DISTRIBUTION *dist;
  int nr;
  int n0, n1;
  int i, j, t, k, p;
  int imin, imax;
  double a, b, c, z, temp;
  double rr_extra[MAXNREC];

  dist = GetEleDist(&i);
  if (i != 0) return;
  temp = dist->params[0];
  z = ion0.atom - ion->nele + 1.0;
  for (i = 0; i < ion->recombined->dim; i++) {
    rec = (RECOMBINED *) ArrayGet(ion->recombined, i);
    if (rec->n_ext == rec->n) continue;
    j = rec->n - 1;
    imin = rec->imin[j];
    imax = rec->imax[j];
    blk0 = ion->iblock[imin];
    n1 = rec->nrec[j];
    a = RRRateHydrogenic(temp, z, n1, &b);
    c = 0.0;
    for (p = 0; p < ion->rr_rates->dim; p++) {
      brts = (BLK_RATE *) ArrayGet(ion->rr_rates, p);
      if (brts->fblock != blk0) continue;
      rates = brts->rates;
      nr = rates->dim;
      for (t = 0; t < nr; t++) {
	r = (RATE *) ArrayGet(rates, t);
	if (r->i == rec->bmin) {
	  c += r->dir;
	}
      }
    }
    c = a/c;
    rr_extra[j] = 1.0 + b/a;
    for (k = rec->n; k < rec->n_ext; k++) {
      n0 = rec->nrec[k];
      b = RRRateHydrogenic(temp, z, n0, NULL);
      rr_extra[k] = b/a;
    }
    a = rr_extra[j];
    for (p = 0; p < ion->rr_rates->dim; p++) {
      brts = (BLK_RATE *) ArrayGet(ion->rr_rates, p);
      if (brts->fblock != blk0) continue;
      rates = brts->rates;
      nr = rates->dim;
      for (t = 0; t < nr; t++) {
	r = (RATE *) ArrayGet(rates, t);
	r->dir *= c;
	for (k = rec->n; k < rec->n_ext; k++) {
	  r0.i = r->i;
	  r0.f = r->f - imin + rec->imin[k];
	  b = rr_extra[k];
	  r0.dir = b*r->dir;
	  r0.inv = 0.0;
	  AddRate(ion, ion->rr_rates, &r0, 0);
	}
	r->dir *= a;
      }
    }
  }
}

void ExtrapolateAI(ION *ion, int inv) {
  RECOMBINED *rec;
  RATE *r, r0;
  LBLOCK *blk0;
  BLK_RATE *brts;
  ARRAY *rates;
  int nr;
  int n0, n1;
  int i, j, t, k, p;
  int imin, imax;
  double a, b, c, e;
  double ai_extra[MAXNREC];

  for (i = 0; i < ion->recombined->dim; i++) {
    rec = (RECOMBINED *) ArrayGet(ion->recombined, i);
    if (rec->n_ext == rec->n) continue;
    j = rec->n - 1;
    imin = rec->imin[j];
    imax = rec->imax[j];
    blk0 = ion->iblock[imin];
    n1 = rec->nrec[j];
    a = 1.0/n1;
    a = a*a*a;
    for (k = rec->n; k < rec->n_ext; k++) {
      n0 = rec->nrec[k];
      b = 1.0/n0;
      b = b*b*b;
      ai_extra[k] = b/a;
    }
    ai_extra[j] = 0.0;
    c = 0.0;
    for (k = rec->nrec[j]+1; k <= ai_extra_nmax; k++) {
      b = 1.0/k;
      b = b*b*b;
      ai_extra[j] += b;
      c += b*b;
    }
    c = (a + b/a)/(a + ai_extra[j]);
    ai_extra[j] = 1.0 + ai_extra[j]/a;
    a = ai_extra[j];
    for (p = 0; p < ion->ai_rates->dim; p++) {
      brts = (BLK_RATE *) ArrayGet(ion->ai_rates, p);
      if (brts->iblock != blk0) continue;
      rates = brts->rates;
      nr = rates->dim;
      for (t = 0; t < nr; t++) {
	r = (RATE *) ArrayGet(rates, t);
	for (k = rec->n; k < rec->n_ext; k++) {
	  r0.i = r->i - imin + rec->imin[k];
	  r0.f = r->f;
	  e = ion->energy[r0.i] - ion->energy[r0.f];
	  if (e < EPS16) continue;
	  b = ai_extra[k];
	  r0.dir = b*r->dir;
	  AIRate(&(r0.dir), &(r0.inv), inv, ion->j[r->i], ion->j[r->f],
		 e, r0.dir/RATE_AU);
	  AddRate(ion, ion->ai_rates, &r0, 0);
	}
	r->inv *= a;
	r->dir *= c;
      }
    }  
  }
}

int SetBlocks(double ni, char *ifn) {
  ION *ion, *ion1 = NULL;
  F_HEADER fh;
  EN_HEADER h;
  EN_RECORD r, *r0, *r1;
  LBLOCK blk, *blkp;
  RECOMBINED *rec, rec0;
  NCOMPLEX ncomplex[MAXNCOMPLEX];
  int bmin, bmax, imin, imax, t, nrec;
  FILE *f;
  int n, i, k, nb, nlevels;
  char *fn;
  int p, q = -1;
  int nionized, n0;
  int swp, endian;

  ion0.n = ni;
  if (ifn) {
    k = strlen(ifn);
    k += 4;
  } else {
    k = 0;
  }
  for (i = 0; i < NDB; i++) {
    if (k > 0) {
      switch (i+1) {
      case DB_EN:
	ion0.dbfiles[i] = (char *) malloc(k);
	sprintf(ion0.dbfiles[i], "%s.en", ifn);
	break;
      case DB_TR:
	ion0.dbfiles[i] = (char *) malloc(k);
	sprintf(ion0.dbfiles[i], "%s.tr", ifn);
	break;
      case DB_CE:
	ion0.dbfiles[i] = (char *) malloc(k);
	sprintf(ion0.dbfiles[i], "%s.ce", ifn);
	break;
      default:
	ion0.dbfiles[i] = NULL;
      }
    } else {
      ion0.dbfiles[i] = NULL;
    }
  }
  
  endian = CheckEndian(NULL);
  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    if (k > 0) {
      if (ion->nele != ion1->nele+1) {
	printf("ERROR: NELE for the added ions are not ");
	printf("a continuous ascending sequence\n");
	exit(1);
      }
      ifn = ion1->dbfiles[DB_EN-1];
    } else {
      ion0.nele = ion->nele - 1;
      ifn = ion0.dbfiles[DB_EN-1];
    }

    fn = ion->dbfiles[DB_EN-1];
    f = fopen(fn, "r");
    if (f == NULL) {
      printf("File %s does not exist\n", fn);
      return -1;
    }
    n = fread(&fh, sizeof(F_HEADER), 1, f);
    if (CheckEndian(&fh) != endian) {
      swp = 1;
      SwapEndianFHeader(&fh);
    } else {
      swp = 0;
    }
    if (k == 0) {
      ion0.atom = fh.atom;
      strcpy(ion0.symbol, fh.symbol);
    }

    nlevels = 0;
    nionized = 0;
    for (i = 0; i < fh.nblocks; i++) {
      n = fread(&h, sizeof(EN_HEADER), 1, f);
      if (swp) SwapEndianENHeader(&h);
      nlevels += h.nlevels;
      if (h.nele == ion->nele-1) {
	nionized += h.nlevels;
      }
      fseek(f, h.length, SEEK_CUR);
    }
    ion->nlevels = nlevels;
    ion->iblock = (LBLOCK **) malloc(sizeof(LBLOCK *)*nlevels);
    ion->ilev = (int *) malloc(sizeof(int)*nlevels);
    ion->j = (short *) malloc(sizeof(short)*nlevels);
    ion->energy = (double *) malloc(sizeof(double)*nlevels);
 
    if (k == 0 && ifn) {
      ion0.nionized = nionized;
      ion0.ionized_map[0] = (int *) malloc(sizeof(int)*nionized);
      ion0.ionized_map[1] = (int *) malloc(sizeof(int)*nionized);
      ion0.energy = (double *) malloc(sizeof(double)*nionized);
    }
    
    fseek(f, sizeof(F_HEADER), SEEK_SET);
    n0 = 0;
    for (nb = 0; nb < fh.nblocks; nb++) {
      n = fread(&h, sizeof(EN_HEADER), 1, f);
      if (swp) SwapEndianENHeader(&h);
      if (h.nele == ion->nele) {
	fseek(f, h.length, SEEK_CUR);
	continue;
      } else if (h.nele == ion->nele-1) {
	r0 = (EN_RECORD *) malloc(sizeof(EN_RECORD)*h.nlevels);
	for (i = 0; i < h.nlevels; i++) {
	  n = fread(&r0[i], sizeof(EN_RECORD), 1, f);
	  if (swp) SwapEndianENRecord(&(r0[i]));
	}
	if (ifn) {
	  r1 = (EN_RECORD *) malloc(sizeof(EN_RECORD)*h.nlevels);
	  nlevels = FindLevelBlock(h.nlevels, r0, r1, ion->nele-1, ifn); 
	  if (nlevels != h.nlevels) {
	    printf("ERROR: Ionized block %d of ion %d ", nb, ion->nele);
	    printf("does not match a block in file %s\n", ifn);
	    exit(1);
	  }
	}
	if (k > 0) {
	  for (i = 0; i < h.nlevels; i++) {
	    p = r0[i].ilev;
	    q = r1[i].ilev;
	    ion->iblock[p] = ion1->iblock[q];
	    ion->ilev[p] = ion1->ilev[q];
	    ion->j[p] = r0[i].j;
	    ion->energy[p] = r0[i].energy;
	  }
	} else {
	  blk.ncomplex[0].n = 0;
	  blk.nlevels = 0;
	  blkp = NULL;
	  for (i = 0; i < h.nlevels; i++) {
	    GetNComplex(ncomplex, r0[i].ncomplex);
	    if (CompareNComplex(ncomplex, blk.ncomplex)) {
	      if (blkp) {
		if (blkp->nlevels > nlevels) {
		  blkp->n = (double *) realloc(blkp->n, 
					       sizeof(double)*nlevels);
		  blkp->n0 = (double *) realloc(blkp->n0, 
						sizeof(double)*nlevels);
		  blkp->r = (double *) realloc(blkp->r, 
					       sizeof(double)*nlevels);
		  blkp->total_rate = (double *)realloc(blkp->total_rate,
						       sizeof(double)*nlevels);
		  blkp->nlevels = nlevels;
		}
	      }
	      nlevels = 0;
	      blk.ib = blocks->dim;
	      blk.iion = -1;
	      blk.irec = -1;
	      blk.rec = NULL;
	      blk.nlevels = h.nlevels;
	      blk.n = (double *) malloc(sizeof(double)*blk.nlevels);
	      blk.n0 = (double *) malloc(sizeof(double)*blk.nlevels);
	      blk.r = (double *) malloc(sizeof(double)*blk.nlevels);
	      blk.total_rate = (double *) malloc(sizeof(double)*blk.nlevels);
	      CopyNComplex(blk.ncomplex, ncomplex);
	      blkp = ArrayAppend(blocks, &blk);
	      q = -1;
	    }
	    p = r0[i].ilev;
	    q++;
	    nlevels++;
	    ion->iblock[p] = blkp;
	    ion->ilev[p] = q;
	    ion->j[p] = r0[i].j;
	    ion->energy[p] = r0[i].energy;
	    if (ifn) {
	      ion0.ionized_map[0][n0] = r1[i].ilev;
	      ion0.ionized_map[1][n0] = r0[i].ilev;
	      ion0.energy[n0] = r1[i].energy;
	      n0++;
	    }
	  }
	}
	free(r0);
	if (ifn) {
	  free(r1);
	}
      } else {
	printf("ERROR: Ion charge state does not match %d %d %d %d\n",
	       k, nb, h.nele, ion->nele);
	exit(1);
      }
    }

    fseek(f, sizeof(F_HEADER), SEEK_SET);
    for (nb = 0; nb < fh.nblocks; nb++) {
      n = fread(&h, sizeof(EN_HEADER), 1, f);
      if (swp) SwapEndianENHeader(&h);
      if (h.nele != ion->nele) {
	fseek(f, h.length, SEEK_CUR);
	continue;
      }	
      blk.ncomplex[0].n = 0;
      blkp = NULL;
      nlevels = 0;
      for (i = 0; i < h.nlevels; i++) {
	n = fread(&r, sizeof(EN_RECORD), 1, f);
	if (swp) SwapEndianENRecord(&r);
	GetNComplex(ncomplex, r.ncomplex);
	if (nb == 0 && i <= n_single_blocks) {
	  nlevels = 0;
	  blk.ib = blocks->dim;
	  blk.iion = k;
	  blk.irec = -1;
	  blk.rec = NULL;
	  if (i < n_single_blocks) {
	    blk.nlevels = 1;
	  } else {
	    blk.nlevels = h.nlevels - n_single_blocks;
	  }
	  blk.n = (double *) malloc(sizeof(double)*blk.nlevels);
	  blk.n0 = (double *) malloc(sizeof(double)*blk.nlevels);
	  blk.r = (double *) malloc(sizeof(double)*blk.nlevels);
	  blk.total_rate = (double *) malloc(sizeof(double)*blk.nlevels);
	  CopyNComplex(blk.ncomplex, ncomplex);
	  blkp = ArrayAppend(blocks, &blk);
	  q = -1;
	} else if (CompareNComplex(ncomplex, blk.ncomplex)) {
	  if (blkp) {
	    if (blkp->nlevels > nlevels) {
	      blkp->n = (double *) realloc(blkp->n, 
					   sizeof(double)*nlevels);
	      blkp->n0 = (double *) realloc(blkp->n0, 
					    sizeof(double)*nlevels);
	      blkp->r = (double *) realloc(blkp->r, 
					   sizeof(double)*nlevels);
	      blkp->total_rate = (double *)realloc(blkp->total_rate,
						   sizeof(double)*nlevels);
	      blkp->nlevels = nlevels;
	    }
	  }
	  nlevels = 0;
	  blk.ib = blocks->dim;
	  blk.iion = k;
	  blk.irec = -1;
	  blk.rec = NULL;
	  blk.nlevels = h.nlevels;
	  blk.n = (double *) malloc(sizeof(double)*blk.nlevels);
	  blk.n0 = (double *) malloc(sizeof(double)*blk.nlevels);
	  blk.r = (double *) malloc(sizeof(double)*blk.nlevels);
	  blk.total_rate = (double *) malloc(sizeof(double)*blk.nlevels);
	  CopyNComplex(blk.ncomplex, ncomplex);
	  blkp = ArrayAppend(blocks, &blk);
	  q = -1;
	}
	
	if (i == 0) {
	  GetRecombined(&t, &nrec, r.name);
	  if (nrec > 0) {
	    imin = r.ilev;
	    bmin = t;
	  }
	} else if (nrec > 0 && i == h.nlevels-1) {
	  GetRecombined(&t, &nrec, r.name);
	  bmax = t;
	  imax = r.ilev;
	  for (t = 0; t < ion->recombined->dim; t++) {
	    rec = (RECOMBINED *) ArrayGet(ion->recombined, t);
	    if (rec->bmin == bmin && rec->bmax == bmax) {
	      rec->imin[rec->n] = imin;
	      rec->imax[rec->n] = imax;
	      rec->nrec[rec->n] = nrec;
	      blkp->irec = rec->n;
	      blkp->rec = rec;
	      rec->n++;
	      break;
	    }
	  }
	  if (t == ion->recombined->dim) {
	    rec0.n = 1;
	    rec0.bmin = bmin;
	    rec0.bmax = bmax;
	    rec0.imin[0] = imin;
	    rec0.imax[0] = imax;
	    rec0.nrec[0] = nrec;
	    blkp->rec = ArrayAppend(ion->recombined, &rec0);
	    blkp->irec = 0;
	  }
	}
	p = r.ilev;
	q++;
	nlevels++;
	ion->iblock[p] = blkp;
	ion->ilev[p] = q;
	ion->j[p] = r.j;
	ion->energy[p] = r.energy;
      }
    }
    if (ion0.n >= 0.0) {
      ExtrapolateEN(k, ion);
    }
    ion1 = ion;
    fclose(f);
  }
  
  k = blocks->dim;
  if (bmatrix) free(bmatrix);
  if (k > 0) {
    k = 2*k*(k+1);
    bmatrix = (double *) malloc(sizeof(double)*k);
  }
  
  return 0;
}

static int CompareENRecord(const void *p0, const void *p1) {
  EN_RECORD *r0, *r1;
  
  r0 = (EN_RECORD *) p0;
  r1 = (EN_RECORD *) p1;
  
  if (r0->j < r1->j) {
    return -1;
  } else if (r0->j > r1->j) {
    return 1;
  } else {
    if (r0->p < r1->p) {
      return -1;
    } else if (r0->p > r1->p) {
      return 1;
    } else {
      if (r0->energy < r1->energy) {
	return -1;
      } else if (r0->energy > r1->energy) {
	return 1;
      } else {
	return 0;
      }
    }
  }
}

int FindLevelBlock(int n, EN_RECORD *r0, EN_RECORD *r1, 
		   int nele, char *ifn) {
  F_HEADER fh;
  EN_HEADER h;
  FILE *f;
  int i, k, nr, nb;
  int swp;

  f = fopen(ifn, "r");
  if (f == NULL) {
    printf("File %s does not exist\n", ifn);
    return -1;
  }
  
  nr = fread(&fh, sizeof(F_HEADER), 1, f);
  if (CheckEndian(&fh) != CheckEndian(NULL)) {
    swp = 1;
    SwapEndianFHeader(&fh);
  } else {
    swp = 0;
  }
  k = 0;
  for (nb = 0; nb < fh.nblocks; nb++) {
    nr = fread(&h, sizeof(EN_HEADER), 1, f);
    if (swp) SwapEndianENHeader(&h);
    if (h.nele != nele) {
      fseek(f, h.length, SEEK_CUR);
      continue;
    }
    k = 0;
    for (i = 0; i < h.nlevels; i++) {
      nr = fread(&r1[k], sizeof(EN_RECORD), 1, f);
      if (swp) SwapEndianENRecord(&(r1[k]));
      if (strcmp(r1[k].ncomplex, r0[0].ncomplex) == 0) {
	k++;
	if (k == n) break;
      }
    }
    if (k == n) break;
  }

  if (k < n) return -1;

  qsort(r0, n, sizeof(EN_RECORD), CompareENRecord);
  qsort(r1, n, sizeof(EN_RECORD), CompareENRecord);

  fclose(f);

  return n;
}

int IonIndex(ION *ion, int i, int k) {
  int m;

  m = 0;
  while (m < ion->nlevels) {
    if (ion->iblock[m]->ib != i) {
      m += ion->iblock[m]->nlevels;
    } else {
      if (ion->ilev[m] == k) return m;
      m++;
    }
  }
  return -1;
}

int IonizedIndex(int i, int m) {
  int k;

  for (k = 0; k < ion0.nionized; k++) {
    if (ion0.ionized_map[m][k] == i) {
      return k;
    }
  }
  
  return -1;
}

int GetNComplex(NCOMPLEX *c, char *s) {
  int i, n, nq;
  char *p;
  
  i = 0;
  while (1) {
    if (i == MAXNCOMPLEX-1) {
      printf("Num of NCOMPLEX shells exceeded the limit %d\n", MAXNCOMPLEX-1);
      exit(1);
    }
    n = strtol(s, &p, 10);
    if (n == 0) {
      for (; i < MAXNCOMPLEX; i++) {
	c[i].n = 0;
	c[i].nq = 0;
      }
      return i;
    }
    s = p+1;
    nq = strtol(s, &p, 10);
    c[i].n = n;
    c[i].nq = nq;
    s = p;
    i++;
  }
}

int StrNComplex(char *s, NCOMPLEX *c) {
  int i;
  char a[8];
  
  i = 0;
  s[0] = '\0';
  while (i < MAXNCOMPLEX && c[i].n) {
    sprintf(a, "%d*%d ", c[i].n, c[i].nq);
    strcat(s, a);
    i++;
  }
  
  return i;
}

int CompareNComplex(NCOMPLEX *c1, NCOMPLEX *c2) {
  int i;

  i = 0;
  while (c1[i].n && c2[i].n) {
    if (c1[i].n == c2[i].n && c1[i].nq == c2[i].nq) i++;
    else return 1;
  }
  if (c1[i].n || c2[i].n) return 1;

  return 0;
}

int CopyNComplex(NCOMPLEX *d, NCOMPLEX *s) {
  int i;
  for (i = 0; i < MAXNCOMPLEX; i++) {
    d[i].n = s[i].n;
    d[i].nq = s[i].nq;
  }
  return 0;
}

int TransitionType(NCOMPLEX *ic, NCOMPLEX *fc) {
  int n1[1024];
  int n2[1024];
  int i, k, k1, k2, m1, m2;

  i = 0;
  for (i = 0; i < 1024; i++) {
    n1[i] = 0;
    n2[i] = 0;
  }
  i = 0;
  while (ic[i].n) {
    n1[ic[i].n] = ic[i].nq;
    i++;
  }
  m1 = ic[i-1].n;
  i = 0;
  while (fc[i].n) {
    n2[fc[i].n] = fc[i].nq;
    i++;
  }
  m2 = fc[i-1].n;

  k1 = 0;
  k2 = 0;
  for (i = 0; i < 1024; i++) {
    k = n1[i] - n2[i];
    if (k == 0) continue;
    else {
      if (k == -1) {
	if (k2) return -1;
	k2 = i;
      }
      else if (k == 1) {
	if (k1) return -1;
	k1 = i;
      }
      else {
	return -1;
      }
    }
  }

  if (k1 == 0 && k2 == 0) return m1*100+m2;
  if (m1 == m2) {
    if (k1 < m1 || k2 < m1) {
      return m1*10000+k1*100+k2;
    }
  } else {
    return k1*100+k2;
  }
}

int SetAbund(int nele, double abund) {
  ION *ion;
  int i;

  if (ion0.nele == nele) ion0.n = abund;
  else {
    for (i = 0; i < ions->dim; i++) {
      ion = (ION *) ArrayGet(ions, i);
      if (ion->nele == nele) {
	ion->n = abund;
	break;
      }
    }
  }
  
  return 0;
}

int InitBlocks(void) {
  ION  *ion;
  RATE *r;
  BLK_RATE *brts;
  LBLOCK *blk1, *blk2;
  int k, m, i, j, p;
  double a, b;

  for (i = 0; i < blocks->dim; i++) {
    blk1 = (LBLOCK *) ArrayGet(blocks, i);
    k = blk1->iion;
    if (k == -1) k = 0;
    ion = (ION *) ArrayGet(ions, k);
    blk1->nb = 1.0;
    for (k = 0; k < blk1->nlevels; k++) {
      blk1->n0[k] = 0.0;
      blk1->n[k] = 0.0;
      blk1->r[k] = 0.0;
      blk1->total_rate[k] = 0.0;
    }
    blk1->r[0] = 1.0;
  }

  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);   
    if (electron_density > 0.0) {
      for (p = 0; p < ion->ce_rates->dim; p++) {
	brts = (BLK_RATE *) ArrayGet(ion->ce_rates, p);
	blk1 = brts->iblock;
	blk2 = brts->fblock;
	for (m = 0; m < brts->rates->dim; m++) {
	  r = (RATE *) ArrayGet(brts->rates, m);
	  j = ion->ilev[r->i];
	  blk1->total_rate[j] += electron_density * r->dir;
	  if (r->inv > 0.0) {
	    j = ion->ilev[r->f];
	    blk2->total_rate[j] += electron_density * r->inv;
	  }
	}
      }
    }
    for (p = 0; p < ion->tr_rates->dim; p++) {
      brts = (BLK_RATE *) ArrayGet(ion->tr_rates, p);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	j = ion->ilev[r->i];
	blk1->total_rate[j] += r->dir;
	blk1->n[j] += r->dir;
	if (r->inv > 0.0 && photon_density > 0.0) {
	  a = photon_density * r->inv;
	  b = a * (ion->j[r->f]+1.0)/(ion->j[r->i]+1.0);
	  blk1->total_rate[j] += b;
	  j = ion->ilev[r->f];
	  blk2->total_rate[j] += a;
	}
      }
    }
    for (p = 0; p < ion->rr_rates->dim; p++) {
      brts = (BLK_RATE *) ArrayGet(ion->rr_rates, p);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	j = ion->ilev[r->i];
	if (electron_density > 0.0) {
	  blk1->total_rate[j] += electron_density * r->dir;
	}
	if (r->inv > 0.0 && photon_density > 0.0) {
	  j = ion->ilev[r->f];
	  blk2->total_rate[j] += photon_density * r->inv;
	}
      }
    }
    for (p = 0; p < ion->ai_rates->dim; p++) {
      brts = (BLK_RATE *) ArrayGet(ion->ai_rates, p);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	j = ion->ilev[r->i];
	blk1->total_rate[j] += r->dir;
	blk1->n[j] += r->dir;
	if (r->inv > 0.0 && electron_density > 0.0) {
	  j = ion->ilev[r->f];
	  blk2->total_rate[j] += electron_density * r->inv;
	}
      }
    }
    if (electron_density > 0.0) {
      for (p = 0; p < ion->ci_rates->dim; p++) {
	brts = (BLK_RATE *) ArrayGet(ion->ci_rates, p);
	blk1 = brts->iblock;
	blk2 = brts->fblock;
	for (m = 0; m < brts->rates->dim; m++) {
	  r = (RATE *) ArrayGet(brts->rates, m);
	  j = ion->ilev[r->i];
	  blk1->total_rate[j] += electron_density * r->dir;
	  if (r->inv > 0.0) {
	    j = ion->ilev[r->f];
	    blk2->total_rate[j] += electron_density * 
	      electron_density * r->inv;
	  }
	}
      }
    }
  }

  for (i = 0; i < blocks->dim; i++) {
    blk1 = (LBLOCK *) ArrayGet(blocks, i);
    for (k = 0; k < blk1->nlevels; k++) {
      if (blk1->n[k] > 0.0) {
	blk1->n[k] = 0.0;
      } else {
	if (blk1->nlevels > 1 && blk1->iion >= 0) {
	  blk1->total_rate[k] = 0.0;
	}
      }
    }
  }
      
  return 0;
}

int RateTable(char *fn, int nc, char *sc[]) { 
  RT_RECORD rt;
  RT_HEADER rt_hdr;
  F_HEADER fhdr;
  ION *ion;
  RATE *r;
  LBLOCK *blk, *blk1;
  BLK_RATE *brts;
  DISTRIBUTION *edist, *pdist;
  int n, k, m, i, j, q;
  double den;
  MULTI ce, tr, ci, rr, ai;
  int ablks[3] = {10, 5, 5};
  int index[3];
  int *ic;
  NCOMPLEX *c, *cp;
  double *d;
  FILE *f;

  edist = GetEleDist(&i);
  pdist = GetPhoDist(&j);
  fhdr.type = DB_RT;
  fhdr.atom = ion0.atom;
  strcpy(fhdr.symbol, ion0.symbol);
  rt_hdr.iedist = i;
  rt_hdr.ipdist = j;
  rt_hdr.np_edist = edist->nparams;
  rt_hdr.np_pdist = pdist->nparams;
  rt_hdr.p_edist = edist->params;
  rt_hdr.p_pdist = pdist->params;

  n = blocks->dim;
  ic = (int *) malloc(sizeof(int)*n);
  MultiInit(&ce, sizeof(double), 3, ablks);
  MultiInit(&tr, sizeof(double), 3, ablks);
  MultiInit(&ci, sizeof(double), 3, ablks);
  MultiInit(&rr, sizeof(double), 3, ablks);
  MultiInit(&ai, sizeof(double), 3, ablks);
  if (nc > 0) {
    c = (NCOMPLEX *) malloc(sizeof(NCOMPLEX)*MAXNCOMPLEX*nc);
    cp = c;
  } else {
    c = NULL;
    cp = NULL;
  }
  for (i = 0; i < nc; i++) {
    GetNComplex(cp, sc[i]);
    cp += MAXNCOMPLEX;
  }
  for (i = 0; i < n; i++) {
    blk = (LBLOCK *) ArrayGet(blocks, i);
    cp = c;
    ic[i] = 0;
    for (j = 0; j < nc; j++) {
      if (CompareNComplex(blk->ncomplex, cp) == 0) {
	ic[i] = 1;
	break;
      }
      cp += MAXNCOMPLEX;
    }
  }
  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    for (q = 0; q < ion->ce_rates->dim; q++) {
      brts = (BLK_RATE *) ArrayGet(ion->ce_rates, q);
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	blk = ion->iblock[r->i];
	blk1 = ion->iblock[r->f];
	i = blk->ib;
	j = blk1->ib;
	if (blk == blk1 && !ic[i]) continue;
	den = blk->n[ion->ilev[r->i]];
	if (den > 0.0) {
	  index[2] = i;
	  index[1] = j;
	  if (ic[j]) {
	    index[0] = ion->ilev[r->f];
	  } else {
	    index[0] = 0;
	  }
	  d = (double *) MultiSet(&ce, index, NULL);
	  *d += den * r->dir;
	}
	if (r->inv > 0.0) {
	  den = blk1->n[ion->ilev[r->f]];
	  if (den > 0.0) {
	    index[2] = j;
	    index[1] = i;
	    if (ic[i]) {
	      index[0] = ion->ilev[r->i];
	    } else {
	      index[0] = 0;
	    }
	    d = (double *) MultiSet(&ce, index, NULL);
	    *d += den * r->inv;
	  }
	}
      }
    }
    for (q = 0; q < ion->tr_rates->dim; q++) {
      brts = (BLK_RATE *) ArrayGet(ion->tr_rates, q);
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	blk = ion->iblock[r->i];
	blk1 = ion->iblock[r->f];
	i = blk->ib;
	j = blk1->ib;
	if (blk == blk1 && !ic[i]) continue;
	den = blk->n[ion->ilev[r->i]];
	if (den > 0.0) {
	  index[2] = i;
	  index[1] = j;
	  if (ic[j]) {
	    index[0] = ion->ilev[r->f];
	  } else {
	    index[0] = 0;
	  }
	  d = (double *) MultiSet(&tr, index, NULL);
	  *d += den * r->dir;
	}
	if (r->inv > 0.0) {
	  den = blk1->n[ion->ilev[r->f]];
	  if (den > 0.0) {
	    index[2] = j;
	    index[1] = i;
	    if (ic[i]) {
	      index[0] = ion->ilev[r->i];
	    } else {
	      index[0] = 0;
	    }
	    d = (double *) MultiSet(&tr, index, NULL);
	    *d += den * r->inv;
	  }
	}
      }
    }
    for (q = 0; q < ion->rr_rates->dim; q++) {
      brts = (BLK_RATE *) ArrayGet(ion->rr_rates, q);
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m); 
	blk = ion->iblock[r->i];
	blk1 = ion->iblock[r->f];
	i = blk->ib;
	j = blk1->ib;
	den = blk->n[ion->ilev[r->i]];
	if (den > 0.0) {
	  index[2] = i;
	  index[1] = j;
	  if (ic[j]) {
	    index[0] = ion->ilev[r->f];
	  } else {
	    index[0] = 0;
	  }
	  d = (double *) MultiSet(&rr, index, NULL);
	  *d += den * r->dir;
	}
	if (r->inv > 0.0) {
	  den = blk1->n[ion->ilev[r->f]];
	  if (den > 0.0) {
	    index[2] = j;
	    index[1] = i;
	    if (ic[i]) {
	      index[0] = ion->ilev[r->i];
	    } else {
	      index[0] = 0;
	    }
	    d = (double *) MultiSet(&rr, index, NULL);
	    *d += den * r->inv;
	  }
	}
      }
    }
    for (q = 0; q < ion->ai_rates->dim; q++) {
      brts = (BLK_RATE *) ArrayGet(ion->ai_rates, q);
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m); 
	blk = ion->iblock[r->i];
	blk1 = ion->iblock[r->f];
	i = blk->ib;
	j = blk1->ib;
	den = blk->n[ion->ilev[r->i]];
	if (den > 0.0) {
	  index[2] = i;
	  index[1] = j;
	  if (ic[j]) {
	    index[0] = ion->ilev[r->f];
	  } else {
	    index[0] = 0;
	  }
	  d = (double *) MultiSet(&ai, index, NULL);
	  *d += den * r->dir;
	}
	if (r->inv > 0.0) {
	  den = blk1->n[ion->ilev[r->f]];
	  if (den > 0.0) {
	    index[2] = j;
	    index[1] = i;
	    if (ic[i]) {
	      index[0] = ion->ilev[r->i];
	    } else {
	      index[0] = 0;
	    }
	    d = (double *) MultiSet(&ai, index, NULL);
	    *d += den * r->inv;
	  }
	}
      }  
    }
    for (q = 0; q < ion->ci_rates->dim; q++) {
      brts = (BLK_RATE *) ArrayGet(ion->ci_rates, q);
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m); 
	blk = ion->iblock[r->i];
	blk1 = ion->iblock[r->f];
	i = blk->ib;
	j = blk1->ib;
	den = blk->n[ion->ilev[r->i]];
	if (den > 0.0) {
	  index[2] = i;
	  index[1] = j;
	  if (ic[j]) {
	    index[0] = ion->ilev[r->f];
	  } else {
	    index[0] = 0;
	  }
	  d = (double *) MultiSet(&ci, index, NULL);
	  *d += den * r->dir;
	}
	if (r->inv > 0.0) {
	  den = blk1->n[ion->ilev[r->f]];
	  if (den > 0.0) {
	    index[2] = j;
	    index[1] = i;
	    if (ic[i]) {
	      index[0] = ion->ilev[r->i];
	    } else {
	      index[0] = 0;
	    }
	    d = (double *) MultiSet(&ci, index, NULL);
	    *d += den * r->inv;
	  }
	}  
      }
    }
  }

  for (i = 0; i < n; i++) {
    blk = (LBLOCK *) ArrayGet(blocks, i);
    k = blk->iion;
    if (k < 0) {
      m = ion0.nele;
      ion = (ION *) ArrayGet(ions, 0);
    } else {
      ion = (ION *) ArrayGet(ions, k);
      m = ion->nele;
    }
    rt_hdr.nele = m;
    rt_hdr.iblock = i;
    StrNComplex(rt_hdr.icomplex, blk->ncomplex);
    index[1] = i;
    if (ic[i]) {
      for (k = 0; k < blk->nlevels; k++) {
	rt_hdr.nb = blk->n[k];
	if (!(blk->n[k])) continue;
	rt_hdr.ilev = IonIndex(ion, i, k);
	if (blk->iion < 0 && ion0.nionized > 0) {
	  rt_hdr.ilev = IonizedIndex(rt_hdr.ilev, 1);
	  rt_hdr.ilev = ion0.ionized_map[0][rt_hdr.ilev];
	}
	f = InitFile(fn, &fhdr, &rt_hdr);
	index[0] = k;
	for (j = 0; j < n; j++) {
	  rt.iblock = j;
	  blk1 = (LBLOCK *) ArrayGet(blocks, j);
	  rt.nb = blk1->nb;
	  index[2] = j;
	  rt.ce = 0.0;
	  rt.tr = 0.0;
	  rt.rr = 0.0;
	  rt.ai = 0.0;
	  rt.ci = 0.0;
	  d = (double *) MultiGet(&ce, index);
	  if (d && *d) {
	    rt.ce = *d;
	  }
	  d = (double *) MultiGet(&tr, index);
	  if (d && *d) {
	    rt.tr = *d;
	  }
	  d = (double *) MultiGet(&rr, index);
	  if (d && *d) {
	    rt.rr = *d;
	  }
	  d = (double *) MultiGet(&ai, index);
	  if (d && *d) {
	    rt.ai = *d;
	  }
	  d = (double *) MultiGet(&ci, index);
	  if (d && *d) {
	    rt.ci = *d;
	  }
	  if (rt.ce || rt.tr ||rt.rr || rt.ai || rt.ci) {
	    StrNComplex(rt.icomplex, blk1->ncomplex);
	    WriteRTRecord(f, &rt);
	  }
	}
	CloseFile(f, &fhdr);
      }
    } else {
      index[0] = 0;
      rt_hdr.nb = blk->nb;
      if (!(blk->nb)) continue;
      rt_hdr.ilev = IonIndex(ion, i, 0);
      if (blk->iion < 0 && ion0.nionized > 0) {
	rt_hdr.ilev = IonizedIndex(rt_hdr.ilev, 1);
	rt_hdr.ilev = ion0.ionized_map[0][rt_hdr.ilev];
      }
      rt_hdr.ilev = -(rt_hdr.ilev+1);
      f = InitFile(fn, &fhdr, &rt_hdr);
      for (j = 0; j < n; j++) {
	rt.iblock = j;
	blk1 = (LBLOCK *) ArrayGet(blocks, j);
	rt.nb = blk1->nb;
	index[2] = j;
	rt.ce = 0.0;
	rt.tr = 0.0;
	rt.rr = 0.0;
	rt.ai = 0.0;
	rt.ci = 0.0;
	d = (double *) MultiGet(&ce, index);
	if (d && *d) {
	  rt.ce = *d;
	} 
	d = (double *) MultiGet(&tr, index);
	if (d && *d) {
	  rt.tr = *d;
	}
	d = (double *) MultiGet(&rr, index);
	if (d && *d) {
	  rt.rr = *d;
	}
	d = (double *) MultiGet(&ai, index);
	if (d && *d) {
	  rt.ai = *d;
	}
	d = (double *) MultiGet(&ci, index);
	if (d && *d) {
	  rt.ci = *d;
	}
	if (rt.ce || rt.tr ||rt.rr || rt.ai || rt.ci) {
	  StrNComplex(rt.icomplex, blk1->ncomplex);
	  WriteRTRecord(f, &rt);
	}
      }
      CloseFile(f, &fhdr);
    }
  }

  free(ic);
  if (c) free(c);
  MultiFree(&ce, NULL);
  MultiFree(&tr, NULL);
  MultiFree(&ci, NULL);
  MultiFree(&rr, NULL);
  MultiFree(&ai, NULL);

  return 0;
}

int BlockMatrix(void) {
  ION *ion;
  RATE *r;
  LBLOCK *blk1, *blk2;
  BLK_RATE *brts;
  int n, k, m, i, j, t;
  int p, q, iion, k0, k1;
  double *x, den, a;
  
  n = blocks->dim;
  for (i = 0; i < 2*n*(n+1); i++) {
    bmatrix[i] = 0.0;
  }
  x = bmatrix + n*n;

  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    if (electron_density > 0.0) {
      for (t = 0; t < ion->ce_rates->dim; t++) {
	brts = (BLK_RATE *) ArrayGet(ion->ce_rates, t);
	blk1 = brts->iblock;
	blk2 = brts->fblock;
	if (blk1 == blk2) continue;
	if (rec_cascade && (blk1->rec || blk2->rec)) continue;
	for (m = 0; m < brts->rates->dim; m++) {
	  r = (RATE *) ArrayGet(brts->rates, m);
	  i = ion->iblock[r->i]->ib;
	  j = ion->iblock[r->f]->ib;
	  den = blk1->r[ion->ilev[r->i]];
	  if (den > 0.0) {
	    p = i*n + j;
	    bmatrix[p] += den * electron_density * r->dir;
	  }
	  if (r->inv > 0.0) {
	    den = blk2->r[ion->ilev[r->f]];
	    if (den > 0.0) {
	      p = i + j*n;
	      bmatrix[p] += den * electron_density * r->inv;
	    }
	  }
	}
      }
    }
    for (t = 0; t < ion->tr_rates->dim; t++) {
      brts = (BLK_RATE *) ArrayGet(ion->tr_rates, t);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      if (blk1 == blk2) continue;
      if (rec_cascade && (blk1->rec || blk2->rec)) continue;
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	i = ion->iblock[r->i]->ib;
	j = ion->iblock[r->f]->ib;
	den = blk1->r[ion->ilev[r->i]];
	if (den > 0.0) {
	  p = i*n + j;
	  bmatrix[p] += den * r->dir;
	}
	if (r->inv > 0.0 && photon_density > 0.0) {
	  if (den > 0.0) {
	    a = photon_density * r->inv;
	    bmatrix[p] += den*a*(ion->j[r->f]+1.0)/(ion->j[r->i]+1.0);
	  }
	  den = blk2->r[ion->ilev[r->f]];
	  if (den > 0.0) {
	    p = i + j*n;
	    bmatrix[p] += den * photon_density * r->inv;
	  }
	}
      }
    }
    for (t = 0; t < ion->rr_rates->dim; t++) {
      brts = (BLK_RATE *) ArrayGet(ion->rr_rates, t);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      if (blk1 == blk2) continue;
      if (rec_cascade && (blk1->rec || blk2->rec)) continue;
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m); 
	i = ion->iblock[r->i]->ib;
	j = ion->iblock[r->f]->ib;
	den = blk1->r[ion->ilev[r->i]];
	if (den > 0.0) {
	  if (electron_density > 0.0) {
	    p = i*n + j;
	    bmatrix[p] += den * electron_density * r->dir;
	  }
	}
	if (r->inv > 0.0 && photon_density > 0.0) {
	  den = blk2->r[ion->ilev[r->f]];
	  if (den > 0.0) {
	    p = i + j*n;
	    bmatrix[p] += den * photon_density * r->inv;
	  }
	}
      }
    }
    for (t = 0; t < ion->ai_rates->dim; t++) {
      brts = (BLK_RATE *) ArrayGet(ion->ai_rates, t);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      if (blk1 == blk2) continue;
      if (rec_cascade && (blk1->rec || blk2->rec)) continue;
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m); 
	i = ion->iblock[r->i]->ib;
	j = ion->iblock[r->f]->ib;
	den = blk1->r[ion->ilev[r->i]];
	if (den > 0.0) {
	  p = i*n + j;
	  bmatrix[p] += den * r->dir;
	}
	if (r->inv > 0.0 && electron_density > 0.0) {
	  den = blk2->r[ion->ilev[r->f]];
	  if (den > 0.0) {
	    p = i + j*n;
	    bmatrix[p] += den * electron_density * r->inv;
	  }
	}
      }  
    }
    if (electron_density > 0.0) {
      for (t = 0; t < ion->ci_rates->dim; t++) {
	brts = (BLK_RATE *) ArrayGet(ion->ci_rates, t);
	blk1 = brts->iblock;
	blk2 = brts->fblock;
	if (blk1 == blk2) continue;
	if (rec_cascade && (blk1->rec || blk2->rec)) continue;
	for (m = 0; m < brts->rates->dim; m++) {
	  r = (RATE *) ArrayGet(brts->rates, m); 
	  i = ion->iblock[r->i]->ib;
	  j = ion->iblock[r->f]->ib;
	  den = blk1->r[ion->ilev[r->i]];
	  if (den > 0.0) {
	    p = i*n + j;
	    bmatrix[p] += den * electron_density * r->dir;
	  }
	  if (r->inv > 0.0) {
	    den = blk2->r[ion->ilev[r->f]];
	    if (den > 0.0) {
	      p = i + j*n;
	      den *= electron_density;
	      bmatrix[p] += den * electron_density * r->inv;
	    }
	  }
	}  
      }
    }
  }

  for (i = 0; i < n; i++) {
    p = i*n;
    q = i + p;
    for (j = 0; j < n; j++) {
      if (j != i) bmatrix[q] += bmatrix[p];
      p++;
    }
    bmatrix[q] = - bmatrix[q];
  }

  iion = -2;
  k0 = 0;
  k1 = 0;
  for (i = 0; i < n; i++) {
    blk1 = (LBLOCK *) ArrayGet(blocks, i);
    if (blk1->iion != iion) {
      if (iion != -2) {
	k = iion;
	if (k == -1) k = 0;
	ion = (ION *) ArrayGet(ions, k);
	if (iion == -1) den = ion0.n;
	else den = ion->n;
	if (den > 0.0) {
	  x[k0] = den;
	  p = k0;
	  for (k = 0; k < n; k++) {
	    if (k < k1 && k >= k0) bmatrix[p] = 1.0;
	    else bmatrix[p] = 0.0;
	    p += n;
	  }
	} 
      }
      iion = blk1->iion;
      k0 = k1;
    }
    k1++;
  }

  k = iion;
  ion = (ION *) ArrayGet(ions, k);
  den = ion->n;
  if (den > 0.0) {
    x[k0] = den;
    p = k0;
    for (k = 0; k < n; k++) {
      if (k < k1 && k >= k0) bmatrix[p] = 1.0;
      else bmatrix[p] = 0.0;
      p += n;
    }
  } 

  return 0;
}

int BlockPopulation(void) {
  LBLOCK *blk;
  double *b, *x;
  double *a;
  int *ipiv;
  int info;
  int n, m;
  int nrhs;
  int lda, ldb;
  int i, j, p, q;

  n = blocks->dim;
  a = bmatrix + n*n;
  x = a;
  ipiv = (int *) bmatrix;
  a = a + n;
  b = a + n*n;

  p = 0;
  q = 0;
  m = 0;
  for (i = 0; i < n; i++) {
    if (1.0+bmatrix[i+i*n] == 1.0) {
      x[i] = -1.0;
      p += n;
      continue;
    }
    for (j = 0; j < n; j++) {
      a[q] = bmatrix[p];
      p++;
      q++;
    }
    b[i] = x[i];
    m++;
  }

  p = 0;
  for (i = 0; i < m; i++) {
    q = p;
    for (j = 0; j < n; j++) {
      if (x[j] < 0.0) {
	continue;
      }
      a[q] = a[p+j];
      q++;
    }
    p += n;
  }

  q = 0;
  for (j = 0; j < n; j++) {
    if (x[j] < 0.0) {
      continue;
    }
    b[q] = b[j];
    q++;
  }  

  nrhs = 1;
  lda = n;
  ldb = n;

  dgesv_(&m, &nrhs, a, &lda, ipiv, b, &ldb, &info);
  if (info != 0) {
    printf("Error in solving BlockMatrix\n");
    exit(1);
  }
  
  p = 0;
  for (i = 0; i < n; i++) {
    blk = (LBLOCK *) ArrayGet(blocks, i);
    if (rec_cascade && blk->rec) continue;
    if (x[i] < 0.0) {
      blk->nb = 0.0;
      for (j = 0; j < blk->nlevels; j++) {
	blk->r[j] = 0.0;
	blk->n[j] = 0.0;
      }
    } else {
      blk->nb = b[p++];
      for (j = 0; j < blk->nlevels; j++) {
	blk->r[j] *= blk->nb;
	blk->n[j] = 0.0;
      }
    }
  }

  return 0;
}
  
double BlockRelaxation(int iter) {
  ION *ion;
  RATE *r;
  LBLOCK *blk1, *blk2;
  BLK_RATE *brts;
  int i, j, k, m, t;
  int p, q;
  double a, b, c, d, h;
  int nlevels;

  if (iter < 0) {
    for (k = 0; k < blocks->dim; k++) {
      blk1 = (LBLOCK *) ArrayGet(blocks, k);
      for (m = 0; m < blk1->nlevels; m++) {
	blk1->r[m] = blk1->n[m];
	blk1->n[m] = 0.0;
      }
    }
  }
  b = 1.0-iter_stablizer;
  c = iter_stablizer;
  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    if (electron_density > 0.0) {
      for (t = 0; t < ion->ce_rates->dim; t++) {
	brts = (BLK_RATE *) ArrayGet(ion->ce_rates, t);
	blk1 = brts->iblock;
	blk2 = brts->fblock;
	if (rec_cascade && iter >= 0) {
	  if (blk1->rec || blk2->rec) continue;
	} 
	for (m = 0; m < brts->rates->dim; m++) {
	  r = (RATE *) ArrayGet(brts->rates, m);
	  i = ion->iblock[r->i]->ib;
	  p = ion->ilev[r->i];
	  j = ion->iblock[r->f]->ib;
	  q = ion->ilev[r->f];
	  if (blk1->r[p] > 0.0) {
	    blk2->n[q] += blk1->r[p] * electron_density * r->dir;
	  }
	  if (r->inv > 0.0) {
	    if (blk2->r[q] > 0.0) {
	      blk1->n[p] += blk2->r[q] * electron_density * r->inv;
	    }
	  }
	}
      }
    }
    for (t = 0; t < ion->tr_rates->dim; t++) {
      brts = (BLK_RATE *) ArrayGet(ion->tr_rates, t);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      if (rec_cascade && iter >= 0) {
	if (blk1->rec || blk2->rec) continue;
      }
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	i = ion->iblock[r->i]->ib;
	p = ion->ilev[r->i];
	j = ion->iblock[r->f]->ib;
	q = ion->ilev[r->f];    
	if (blk1->r[p] > 0.0) {
	  blk2->n[q] += blk1->r[p] * r->dir;
	}
	if (r->inv > 0.0 && photon_density > 0.0) {
	  a = photon_density * r->inv;
	  if (blk1->r[p] > 0.0) {
	    blk2->n[q] += blk1->r[p]*a*(ion->j[r->f]+1.0)/(ion->j[r->i]+1.0);
	  }
	  if (blk2->r[q] > 0.0) {
	    blk1->n[p] += blk2->r[q] * a;
	  }
	}
      }
    }
    for (t = 0; t < ion->rr_rates->dim; t++) {
      brts = (BLK_RATE *) ArrayGet(ion->rr_rates, t);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      if (rec_cascade && iter >= 0) {
	if (blk1->rec || blk2->rec) continue;
      }
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	i = ion->iblock[r->i]->ib;
	p = ion->ilev[r->i];
	j = ion->iblock[r->f]->ib;
	q = ion->ilev[r->f];    
	if (electron_density > 0.0) {
	  if (blk1->r[p] > 0.0) {
	    blk2->n[q] += blk1->r[p] * r->dir;
	  }
	} 
	if (r->inv > 0.0 && photon_density > 0.0) {
	  if (blk2->r[q] > 0.0) {
	    blk1->n[p] += blk2->r[q] * photon_density * r->inv;
	  }
	}
      }
    }
    for (t = 0; t < ion->ai_rates->dim; t++) {
      brts = (BLK_RATE *) ArrayGet(ion->ai_rates, t);
      blk1 = brts->iblock;
      blk2 = brts->fblock;
      if (rec_cascade && iter >= 0) {
	if (blk1->rec || blk2->rec) continue;
      }
      for (m = 0; m < brts->rates->dim; m++) {
	r = (RATE *) ArrayGet(brts->rates, m);
	i = ion->iblock[r->i]->ib;
	p = ion->ilev[r->i];
	j = ion->iblock[r->f]->ib;
	q = ion->ilev[r->f];    
	if (blk1->r[p] > 0.0) {
	  blk2->n[q] += blk1->r[p] * r->dir;
	}
	if (r->inv > 0.0 && electron_density > 0.0) {
	  if (blk2->r[q] > 0.0) {
	    blk1->n[p] += blk2->r[q] * electron_density * r->inv;
	  }
	}
      }
    }
    if (electron_density > 0.0) {
      for (t = 0; t < ion->ci_rates->dim; t++) {
	brts = (BLK_RATE *) ArrayGet(ion->ci_rates, t);
	blk1 = brts->iblock;
	blk2 = brts->fblock;
	if (rec_cascade && iter >= 0) {
	  if (blk1->rec || blk2->rec) continue;
	}
	for (m = 0; m < brts->rates->dim; m++) {
	  r = (RATE *) ArrayGet(brts->rates, m);
	  i = ion->iblock[r->i]->ib;
	  p = ion->ilev[r->i];
	  j = ion->iblock[r->f]->ib;
	  q = ion->ilev[r->f];    
	  if (blk1->r[p] > 0.0) {
	    blk2->n[q] += blk1->r[p] * electron_density * r->dir;
	  }
	  if (r->inv) {
	    if (blk2->r[q] > 0.0) {
	      blk1->n[p] += blk2->r[q] * electron_density * r->inv;
	    }
	  }
	}
      }
    }
  }

  nlevels = 0;
  d = 0.0;
  for (k = 0; k < blocks->dim; k++) {
    blk1 = (LBLOCK *) ArrayGet(blocks, k);
    if (rec_cascade && iter >= 0) {
      if (blk1->rec) continue;
    }
    a = 0.0;
    for (m = 0; m < blk1->nlevels; m++) {
      if (1.0+blk1->total_rate[m] != 1.0) {
	blk1->n[m] /= blk1->total_rate[m];
	a += blk1->n[m];
      } else {
	blk1->n[m] = 0.0;
      }
    }
    if (iter < 0) blk1->nb = a;

    if (a == 0.0 || blk1->nb == 0.0) {
      for (m = 0; m < blk1->nlevels; m++) {
	blk1->n[m] = 0.0;
	blk1->r[m] = 0.0;
	blk1->n0[m] = 0.0;
      }
      blk1->r[0] = 1.0;
      continue;
    }
    
    if (iter >= 0) {
      a = blk1->nb/a;
      for (m = 0; m < blk1->nlevels; m++) {
	blk1->n[m] *= a;
	if (blk1->n[m] > 0.0) {
	  d += fabs(1.0 - blk1->n0[m]/blk1->n[m]);
	  nlevels += 1;
	}
	if (iter >= 2) {
	  blk1->n[m] = b*blk1->n0[m] + c*blk1->n[m];
	}
	blk1->r[m] = blk1->n[m]/blk1->nb;
	blk1->n0[m] = blk1->n[m];      
      }
    }
  }    
  
  if (iter < 0) {
    q = 0;
    p = -1;
    a = 0.0;
    for (k = 0; k < blocks->dim; k++) {
      blk1 = (LBLOCK *) ArrayGet(blocks, k);
      if (blk1->iion != p) {
	if (p == -1) {
	  h = ion0.n;
	} else {
	  ion = (ION *) ArrayGet(ions, p);
	  h = ion->n;
	}
	if (h > 0.0) {
	  a = h/a;
	  for (i = q; i < k; i++) {
	    blk2 = (LBLOCK *) ArrayGet(blocks, i);
	    blk2->nb *= a;
	    for (m = 0; m < blk2->nlevels; m++) {
	      blk2->n[m] *= a;
	    }
	  }
	} 
	p = blk1->iion;
	a = 0.0;
	q = k;
      }
      a += blk1->nb;
    }
    ion = (ION *) ArrayGet(ions, p);
    if (ion->n > 0.0) {
      a = ion->n/a;
      for (i = q; i < k; i++) {
	blk2 = (LBLOCK *) ArrayGet(blocks, i);
	blk2->nb *= a;
	for (m = 0; m < blk2->nlevels; m++) {
	  blk2->n[m] *= a;
	}
      }
    }
    for (k = 0; k < blocks->dim; k++) {
      blk1 = (LBLOCK *) ArrayGet(blocks, k);
      for (m = 0; m < blk1->nlevels; m++) {
	if (blk1->n[m] > 0.0 && blk1->rec == NULL) {
	  d += fabs(1.0 - blk1->n0[m]/blk1->n[m]);
	  nlevels += 1;
	}
	if (iter <= -2) {
	  blk1->n[m] = b*blk1->n0[m] + c*blk1->n[m];
	}
	blk1->r[m] = blk1->n[m]/blk1->nb;
	blk1->n0[m] = blk1->n[m];
      }
    }
  }
	    
  d /= nlevels;
  return d;
}

int LevelPopulation(void) {
  int i;
  double d;

  printf("Populate Iteration:\n");
  for (i = 0; i < max_iter; i++) {
    BlockMatrix();
    BlockPopulation();
    d = BlockRelaxation(i);
    printf("%5d %11.4E\n", i, d);
    if (d < iter_accuracy) break;
  }
 
  if (i == max_iter) {
    printf("Max iteration reached\n");
  }
  return 0;
}

int Cascade(void) {
  int i;
  double d;
  
  if (!rec_cascade) return 0;
  printf("Cascade  Iteration:\n");
  d = BlockRelaxation(-1);
  for (i = 1; i <= max_iter; i++) {
    d = BlockRelaxation(-i);
    printf("%5d %11.4E\n", i, d);
    if (d < cas_accuracy) break;
  }
  
  if (i == max_iter) {
    printf("Max iteration reached in Cascade\n");
  }

  return 0;
}

int SpecTable(char *fn, double strength_threshold) {
  SP_RECORD r;
  SP_HEADER sp_hdr;
  F_HEADER fhdr;
  ION *ion;
  RATE *rt;
  LBLOCK *blk, *iblk, *fblk;
  BLK_RATE *brts;
  DISTRIBUTION *edist, *pdist;
  int k, m, j;
  FILE *f;
  double e, a;
  int i, p, q, ib;
  int iedist, ipdist;
  double smax, s;

  edist = GetEleDist(&iedist);
  pdist = GetPhoDist(&ipdist);
  fhdr.type = DB_SP;
  fhdr.atom = ion0.atom;
  strcpy(fhdr.symbol, ion0.symbol);
  sp_hdr.eden = electron_density;
  sp_hdr.iedist = iedist;
  sp_hdr.np_edist = edist->nparams;
  sp_hdr.p_edist = edist->params;
  sp_hdr.pden = photon_density;
  sp_hdr.ipdist = ipdist;
  sp_hdr.np_pdist = pdist->nparams;
  sp_hdr.p_pdist = pdist->params;

  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    sp_hdr.type = 0;
    ib = -1;
    f = NULL;
    for (m = 0; m < ion->nlevels; m++) {
      blk = ion->iblock[m];
      i = blk->ib;
      if (i != ib) {
	if (f) CloseFile(f, &fhdr);
	if (blk->iion != k) {
	  sp_hdr.nele = ion->nele - 1;
	} else {
	  sp_hdr.nele = ion->nele;
	}
	sp_hdr.iblock = i;
	StrNComplex(sp_hdr.icomplex, blk->ncomplex);
	sp_hdr.fblock = 0;
	sp_hdr.fcomplex[0] = '\0';
	f = InitFile(fn, &fhdr, &sp_hdr);
	ib = i;
      }
      p = ion->ilev[m];
      if (blk->n[p] > 0.0) {
	r.upper = m;
	r.lower = p;
	r.energy = ion->energy[m];
	r.strength = blk->n[p];
	WriteSPRecord(f, &r);
      }
    }
    if (f) CloseFile(f, &fhdr);

    for (i = 0; i < ion->tr_rates->dim; i++) {
      brts = (BLK_RATE *) ArrayGet(ion->tr_rates, i);
      if (brts->rates->dim == 0) continue;
      iblk = brts->iblock;
      fblk = brts->fblock;
      if (iblk->iion != k) {
	sp_hdr.nele = ion->nele - 1;
      } else {
	sp_hdr.nele = ion->nele;
      }
      sp_hdr.iblock = iblk->ib;
      sp_hdr.fblock = fblk->ib;
      sp_hdr.type = TransitionType(iblk->ncomplex, fblk->ncomplex);
      StrNComplex(sp_hdr.icomplex, iblk->ncomplex);
      StrNComplex(sp_hdr.fcomplex, fblk->ncomplex);
      f = InitFile(fn, &fhdr, &sp_hdr);
      smax = 0.0;
      for (m = 0; m < brts->rates->dim; m++) {
	rt = (RATE *) ArrayGet(brts->rates, m);
	if (k == 0 && 
	    ion0.nionized > 0 &&
	    (p = IonizedIndex(rt->i, 1)) >= 0 &&
	    (q = IonizedIndex(rt->f, 1)) >= 0) {
	  e = ion0.energy[p] - ion0.energy[q];
	  p = ion0.ionized_map[0][p];
	  q = ion0.ionized_map[0][q];
	} else {
	  p = rt->i;
	  q = rt->f;
	  e = ion->energy[p] - ion->energy[q];
	}
	j = ion->ilev[rt->i];
	if (iblk->n[j] > 0.0) {
	  r.lower = q;
	  r.upper = p;
	  r.energy = e;
	  r.strength = iblk->n[j] * rt->dir;
	  if (rt->inv > 0.0 && photon_density > 0.0) {
	    a = photon_density * rt->inv;
	    a *= (ion->j[rt->f]+1.0)/(ion->j[rt->i]+1.0);
	    r.strength += iblk->n[j] * a;
	  }
	  s = r.strength*e;
	  if (s < strength_threshold*smax) continue;
	  if (s > smax) smax = s;
	  WriteSPRecord(f, &r);
	}
      }
      CloseFile(f, &fhdr);
    }
  }  
  return 0;
}

static int CompareLine(const void *p1, const void *p2) {
  double *v1, *v2;
  v1 = (double *) p1;
  v2 = (double *) p2;
  if (*v1 < *v2) return -1;
  else if (*v1 > *v2) return 1;
  else return 0;
}

int SelectLines(char *ifn, char *ofn, int nele, int type, 
		double emin, double emax, double fmin) {
  F_HEADER fh;
  SP_HEADER h;
  SP_RECORD r, *rp;
  DISTRIBUTION *dist;
  ARRAY sp;
  FILE *f1, *f2;
  int n, nb, i, m;
  int t, t0, t1, t2;
  int r0, r1;
  int low, up;
  double e, a, smax;
  int swp;
  
  if (type == 0) {
    printf("Type must not be 0\n");
    return -1;
  }
  
  f1 = fopen(ifn, "r");
  if (f1 == NULL) {
    printf("ERROR: File %s does not exist\n", ifn);
    return -1;
  }
  f2 = fopen(ofn, "a");
  if (f2 == NULL) {
    printf("ERROR: Cannot open file %s\n", ofn);
    return -1;
  }
  dist = GetEleDist(&i);
  
  t2 = type / 1000000;
  t = type % 1000000;
  t1 = t / 10000;
  t0 = t % 10000;
  t0 = t0/100; 

  if (fmin < 0.0) {
    low = emin;
    up = emax;
  }
  n = fread(&fh, sizeof(F_HEADER), 1, f1);
  if (CheckEndian(&fh) != CheckEndian(NULL)) {
    swp = 1;
    SwapEndianFHeader(&fh);
  } else {
    swp = 0;
  }
  ArrayInit(&sp, sizeof(SP_RECORD), 512);
  smax = 0.0;
  for (nb = 0; nb < fh.nblocks; nb++) {
    n = fread(&h, sizeof(SP_HEADER), 1, f1);
    if (swp) SwapEndianSPHeader(&h);
    m = sizeof(double)*(h.np_edist + h.np_pdist);
    fseek(f1, m, SEEK_CUR);    
    if (h.ntransitions == 0) continue;
    if (h.nele != nele) goto LOOPEND;  
    r1 = h.type / 10000;
    r0 = h.type % 10000;
    r0 = r0/100;
    if (t2 == 0) {
      if (t != h.type) goto LOOPEND;
    } else if (t2 == 1) {
      if (r1 < t1) goto LOOPEND;
      if (h.type < 100) goto LOOPEND;
      if (t%10000 != h.type%10000) goto LOOPEND;
    } else {
      if (t < 100) {
	if (h.type > 99) goto LOOPEND;
	if (h.type < t) goto LOOPEND;
      } else {
	if (r1 < t1) goto LOOPEND;
	if (r0 < t0) goto LOOPEND;
      }
    }
    for (i = 0; i < h.ntransitions; i++) {
      n = fread(&r, sizeof(SP_RECORD), 1, f1);
      if (swp) SwapEndianSPRecord(&r);
      r.energy *= HARTREE_EV;
      if (fmin < 0.0) {
	if (r.lower == low && r.upper == up) {
	  ArrayAppend(&sp, &r);
	  break;
	}
      } else {
	if (r.energy < emin || r.energy > emax) continue;
	a = r.energy * r.strength;
	if (a < smax*fmin) continue;
	if (a > smax) smax = a;
	ArrayAppend(&sp, &r);
      }
    }
    continue;

  LOOPEND:
    fseek(f1, h.length, SEEK_CUR);
  }

  if (fmin < 0.0) {
    if (sp.dim > 0) {
      rp = (SP_RECORD *) ArrayGet(&sp, 0);
      fprintf(f2, "%2d %6d %6d %15.8E %15.8E\n", 
	      nele, rp->lower, rp->upper, rp->energy, rp->strength);
    }
  } else {
    if (sp.dim > 0) {
      smax *= fmin;
      for (i = 0; i < sp.dim; i++) {
	rp = (SP_RECORD *) ArrayGet(&sp, i);
	e = rp->energy;
	if (rp->strength*e > smax) {
	  fprintf(f2, "%2d %6d %6d %15.8E %15.8E\n", 
		  nele, rp->lower, rp->upper, e, rp->strength);
	}
      }
    }
  }	
  ArrayFree(&sp, NULL);

  fclose(f1);
  fclose(f2);
  
  return 0;
}
    
int PlotSpec(char *ifn, char *ofn, int nele, int type, 
	     double emin, double emax, double de, double smin) {
  F_HEADER fh;
  SP_HEADER h;
  SP_RECORD r;
  DISTRIBUTION *dist;
  FILE *f1, *f2;
  int n, nb, i;
  int t, t0, t1, t2;
  int r0, r1;
  double e;
  int m, k, nsp;
  double *sp, *tsp, *xsp, *kernel;
  double de10, de01;
  double a, sig, factor;
  double *lines;
  double smax;
  int swp;

  if (type == 0) {
    printf("Type must not be 0\n");
    return -1;
  }
  
  f1 = fopen(ifn, "r");
  if (f1 == NULL) {
    printf("ERROR: File %s does not exist\n", ifn);
    return -1;
  }
  f2 = fopen(ofn, "a");
  if (f2 == NULL) {
    printf("ERROR: Cannot open file %s\n", ofn);
    return -1;
  }

  dist = GetEleDist(&i);
  
  de01 = 0.1*de;
  de10 = 10.0*de;
  sig = de/2.35;
  factor = 1.0/(sqrt(2*PI)*sig);
  sig = 1.0/(2*sig*sig);
  kernel = (double *) malloc(sizeof(double)*128);
  e = -63.5*de01;
  for (i = 0; i < 128; i++){
    kernel[i] = factor*exp(-sig*e*e);
    e += de01;
  }

  nsp = (emax - emin)/de01;
  sp = (double *) malloc(sizeof(double)*nsp);
  xsp = (double *) malloc(sizeof(double)*nsp);
  tsp = (double *) malloc(sizeof(double)*nsp);
  sp[0] = 0.0;
  tsp[0] = 0.0;
  xsp[0] = emin;
  for (i = 1; i < nsp; i++) {
    tsp[i] = 0.0;
    sp[i] = 0.0;
    xsp[i] = xsp[i-1] + de01;
  }
  
  t2 = type / 1000000;
  t = type % 1000000;
  t1 = t / 10000;
  t0 = t % 10000;
  t0 = t0 / 100;

  n = fread(&fh, sizeof(F_HEADER), 1, f1);
  if (CheckEndian(&fh) != CheckEndian(NULL)) {
    swp = 1;
    SwapEndianFHeader(&fh);
  } else {
    swp = 0;
  }

  for (nb = 0; nb < fh.nblocks; nb++) {
    n = fread(&h, sizeof(SP_HEADER), 1, f1);
    if (swp) SwapEndianSPHeader(&h);
    m = sizeof(double)*(h.np_edist + h.np_pdist);
    fseek(f1, m, SEEK_CUR);
    if (h.ntransitions == 0) continue;
    if (h.nele != nele) goto LOOPEND; 
    r1 = h.type / 10000;
    r0 = h.type % 10000;
    r0 = r0/100;
    if (t2 == 0) {
      if (t != h.type) goto LOOPEND;
    } else if (t2 == 1) {
      if (r1 < t1) goto LOOPEND;
      if (h.type < 100) goto LOOPEND;
      if (t%10000 != h.type%10000) goto LOOPEND;
    } else {
      if (t < 100) {
	if (h.type > 99) goto LOOPEND;
	if (h.type < t) goto LOOPEND;
      } else {
	if (r1 < t1) goto LOOPEND;
	if (r0 < t0) goto LOOPEND;
      }
    }
    if (h.type > 99) {
      m = 2*h.ntransitions;
      lines = (double *) malloc(sizeof(double)*m);  
      k = 0;
      smax = 0.0;
      for (i = 0; i < h.ntransitions; i++) {
	n = fread(&r, sizeof(SP_RECORD), 1, f1);
	if (swp) SwapEndianSPRecord(&r);
	e = r.energy;
	a = r.strength * e;
	if (a < smax*smin) continue;
	if (a > smax) smax = a;
	e *= HARTREE_EV;
	lines[k++] = e;
	lines[k++] = r.strength;
      }
      m = k;
	
      qsort(lines, m/2, sizeof(double)*2, CompareLine);
      k = 0;
      i = 0;
      while (k < m && i < nsp-1) {
	if (lines[k] < xsp[i]) {
	  k += 2;
	} else if (lines[k] < xsp[i+1]) {
	  sp[i] += lines[k+1];
	  k += 2;
	} else {
	  i++;
	}
      } 
      free(lines);
      for (i = 0; i < nsp; i++) {
	if (sp[i] > 0.0) {
	  for (m = i-64, k = 0; k < 128; k++, m++) {
	    if (m > 0 && m < nsp) tsp[m] += sp[i]*kernel[k];
	  }
	}
	sp[i] = 0.0;
      }
      continue;
    } else {
      printf("plotting RR continuum not implemented yet\n");
      fseek(f1, h.length, SEEK_CUR);
      continue;
    }
  LOOPEND:
    fseek(f1, h.length, SEEK_CUR);
  }

  for (i = 0; i < nsp; i++) {
    fprintf(f2, "%15.8E\t%15.8E\n", xsp[i], tsp[i]);
  }
  fprintf(f2, "\n\n");

  free(xsp);
  free(sp);
  free(tsp);
  free(kernel);

  fclose(f1);
  fclose(f2);

  return 0;
}

void AddRate(ION *ion, ARRAY *rts, RATE *r, int m) {
  LBLOCK *ib, *fb;
  BLK_RATE *brt, brt0;
  RATE *r0;
  int i;
  
  ib = ion->iblock[r->i];
  fb = ion->iblock[r->f];
  for (i = 0; i < rts->dim; i++) {
    brt = (BLK_RATE *) ArrayGet(rts, i);
    if (brt->iblock == ib && brt->fblock == fb) {
      break;
    }
  }
  if (i == rts->dim) {
    brt0.iblock = ib;
    brt0.fblock = fb;
    brt0.rates = (ARRAY *) malloc(sizeof(ARRAY));
    ArrayInit(brt0.rates, sizeof(RATE), RATES_BLOCK);
    ArrayAppend(brt0.rates, r);
    ArrayAppend(rts, &brt0);
  } else {
    if (m) {
      for (i = 0; i < brt->rates->dim; i++) {
	r0 = (RATE *) ArrayGet(brt->rates, i);
	if (r0->i == r->i && r0->f == r0->f) break;
      }
      if (i == brt->rates->dim) {
	ArrayAppend(brt->rates, r);
      } else {
	r0->dir += r->dir;
	r0->inv += r->inv;
      }
    } else {
      ArrayAppend(brt->rates, r);
    }
  }
}
  
int SetCERates(int inv) {
  int nb, i, j, t;
  int n, m, m1, k;
  int j1, j2;
  int p, q;
  ION *ion;
  RATE rt;
  F_HEADER fh;
  CE_HEADER h;
  CE_RECORD r;
  FILE *f;
  double e;
  float cs[MAXNUSR];
  double data[1+(1+MAXNUSR)*4];
  double *y, *x, *x2, *logx;
  double eusr[MAXNUSR];
  int swp, endian;
  
  endian = CheckEndian(NULL);
  if (ion0.atom <= 0) {
    printf("ERROR: Blocks not set, exitting\n");
    exit(1);
  }
  y = data + 1;
  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    ArrayFree(ion->ce_rates, NULL);
    f = fopen(ion->dbfiles[DB_CE-1], "r");
    if (f == NULL) {
      printf("File %s does not exist, skipping.\n", ion->dbfiles[DB_CE-1]);
      continue;
    }
    n = fread(&fh, sizeof(F_HEADER), 1, f);
    if (CheckEndian(&fh) != endian) {
      swp = 1;
      SwapEndianFHeader(&fh);
    } else {
      swp = 0;
    }
    for (nb = 0; nb < fh.nblocks; nb++) {
      n = fread(&h, sizeof(CE_HEADER), 1, f);
      if (swp) SwapEndianCEHeader(&h);
      m = h.n_tegrid + h.n_egrid;
      fseek(f, sizeof(double)*m, SEEK_CUR);
      m = h.n_usr;
      n = fread(eusr, sizeof(double), m, f);
      if (swp) {
	for (i = 0; i < m; i++) {
	  SwapEndian((char *) &(eusr[i]), sizeof(double));
	}
      }
      if (h.nele == ion->nele-1) {
	if (k > 0 || ion0.nionized > 0) {
	  fseek(f, h.length, SEEK_CUR);
	  continue;
	}
      }
      m1 = m + 1;
      x = y + m1;
      x2 = x + m1;
      logx = x2 + m1;
      x[0] = 0.0;
      x2[0] = 0.0;
      for (i = 0; i < h.ntransitions; i++) {
	n = fread(&r, sizeof(CE_RECORD), 1, f);
	if (swp) SwapEndianCERecord(&r);
	if (h.nparams > 0) {
	  fseek(f, sizeof(float)*h.nparams, SEEK_CUR);
	}
	rt.i = r.lower;
	rt.f = r.upper;
	j1 = ion->j[r.lower];
	j2 = ion->j[r.upper];
	e = ion->energy[r.upper] - ion->energy[r.lower];
	data[0] = r.bethe;	
	n = fread(cs, sizeof(float), m, f);
	if (swp) {
	  for (j = 0; j < m; j++) {
	    SwapEndian((char *) &(cs[j]), sizeof(float));
	  }
	}
	if (r.bethe < 0.0) {
	  y[0] = 0.0;
	  for (j = 0; j < m; j++) {
	    t = m-j;
	    x[t] = e/(e + eusr[j]);
	    x2[t] = x[t]*x[t];
	    y[t] = cs[j];
	  }
	} else {
	  if (r.bethe > 0.0) {
	    for (j = 0; j < m; j++) {
	      t = m-j;
	      x[t] = e/(e + eusr[j]);
	      logx[j] = log(x[t]);
	      y[t] = cs[j] + r.bethe*logx[j];
	    }
	  } else {
	    for (j = 0; j < m; j++) {
	      t = m-j;
	      x[t] = e/(e + eusr[j]);
	      y[t] = cs[j];
	    }
	  }
	  n = 3;
	  m1 = 1;
	  uvip3p_(&n, &m, &(x[1]), &(y[1]), &m1, &(x[0]), &(y[0]));
	  if (r.bethe + 1.0 == 1.0) {
	    if (y[0] < 0.0) y[0] = 0.0;
	  }
	}
	CERate(&(rt.dir), &(rt.inv), inv, j1, j2, e, m,
	       data, rt.i, rt.f);
	AddRate(ion, ion->ce_rates, &rt, 0);
      }
    }
    fclose(f);
    
    if (k == 0 && ion0.nionized > 0) {
      f = fopen(ion0.dbfiles[DB_CE-1], "r");
      if (f == NULL) {
	printf("File %s does not exist, skipping.\n", ion0.dbfiles[DB_CE-1]);
	continue;
      }
      n = fread(&fh, sizeof(F_HEADER), 1, f);
      if (CheckEndian(&fh) != endian) {
	swp = 1;
	SwapEndianFHeader(&fh);
      } else {
	swp = 0;
      }
      for (nb = 0; nb < fh.nblocks; nb++) {
	n = fread(&h, sizeof(CE_HEADER), 1, f);
	if (swp) SwapEndianCEHeader(&h);
	m = h.n_tegrid + h.n_egrid;
	fseek(f, sizeof(double)*m, SEEK_CUR);
	m = h.n_usr;   
	n = fread(eusr, sizeof(double), m, f);
	if (swp) {
	  for (i = 0; i < m; i++) {
	    SwapEndian((char *) &(eusr[i]), sizeof(double));
	  }
	}
	if (h.nele != ion0.nele) {
	  fseek(f, h.length, SEEK_CUR);
	  continue;
	}
	m1 = m + 1;
	x = y + m1;
	x2 = x + m1;
	logx = x2 + m1;
	x[0] = 0.0;
	x2[0] = 0.0;
	for (i = 0; i < h.ntransitions; i++) {
	  n = fread(&r, sizeof(CE_RECORD), 1, f);
	  if (swp) SwapEndianCERecord(&r);
	  p = IonizedIndex(r.lower, 0);
	  if (p < 0) {
	    fseek(f, sizeof(float)*(h.nparams+h.n_usr), SEEK_CUR);
	    continue;
	  }
	  q = IonizedIndex(r.upper, 0);
	  if (q < 0) {
	    fseek(f, sizeof(float)*(h.nparams+h.n_usr), SEEK_CUR);
	    continue;
	  }
	  if (h.nparams > 0) {
	    fseek(f, sizeof(float)*h.nparams, SEEK_CUR);
	  }
	  rt.i = ion0.ionized_map[1][p];
	  rt.f = ion0.ionized_map[1][q];
	  j1 = ion->j[rt.i];
	  j2 = ion->j[rt.f];
	  e = ion0.energy[q] - ion0.energy[p];
	  data[0] = r.bethe;
	  n = fread(cs, sizeof(float), m, f);
	  if (swp) {
	    for (j = 0; j < m; j++) {
	      SwapEndian((char *) &(cs[j]), sizeof(float));
	    }
	  }
	  if (r.bethe < 0.0) {
	    y[0] = 0.0;
	    for (j = 0; j < m; j++) {
	      t = m-j;
	      x[t] = e/(e + eusr[j]);
	      x2[t] = x[t]*x[t];
	      y[t] = cs[j];
	    }
	  } else {
	    if (r.bethe > 0.0) {
	      for (j = 0; j < m; j++) {
		t = m-j;
		x[t] = e/(e + eusr[j]);
		logx[j] = log(x[t]);
		y[t] = cs[j] + r.bethe*logx[j];
	      }
	    } else {
	      for (j = 0; j < m; j++) {
		t = m-j;
		x[t] = e/(e + eusr[j]);
		y[t] = cs[j];
	      }
	    }
	    n = 3;
	    m1 = 1;
	    uvip3p_(&n, &m, &(x[1]), &(y[1]), &m1, &(x[0]), &(y[0]));
	    if (r.bethe + 1.0 == 1.0) {
	      if (y[0] < 0.0) y[0] = 0.0;
	    }
	  }
	  CERate(&(rt.dir), &(rt.inv), inv, j1, j2, e, m,
		 data, rt.i, rt.f);
	  AddRate(ion, ion->ce_rates, &rt, 0);
	}
      }
      fclose(f);
    }
  }
  return 0;
}

int SetTRRates(int inv) {
  int nb, i;
  int n, k;
  int j1, j2;
  int p, q, m;
  ION *ion;
  RATE rt;
  F_HEADER fh;
  TR_HEADER h;
  TR_RECORD r;
  LBLOCK *ib;
  double e;
  FILE *f;  
  int swp, endian;

  endian = CheckEndian(NULL);
  if (ion0.atom <= 0) {
    printf("ERROR: Blocks not set, exitting\n");
    exit(1);
  }
  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    ArrayFree(ion->tr_rates, NULL);
    f = fopen(ion->dbfiles[DB_TR-1], "r");
    if (f == NULL) {
      printf("File %s does not exist, skipping.\n", ion->dbfiles[DB_TR-1]);
      continue;
    }
    n = fread(&fh, sizeof(F_HEADER), 1, f);
    if (CheckEndian(&fh) != endian) {
      swp = 1;
      SwapEndianFHeader(&fh);
    } else {
      swp = 0;
    }
    for (nb = 0; nb < fh.nblocks; nb++) {
      n = fread(&h, sizeof(TR_HEADER), 1, f);
      if (swp) SwapEndianTRHeader(&h);
      if (h.nele == ion->nele-1) {
	if (k > 0 || ion0.nionized > 0) {
	  fseek(f, h.length, SEEK_CUR);
	  continue;
	}
      }
      if (abs(h.multipole) == 1) m = 0;
      else m = 1;
      for (i = 0; i < h.ntransitions; i++) {
	n = fread(&r, sizeof(TR_RECORD), 1, f);
	if (swp) SwapEndianTRRecord(&r);
	rt.i = r.upper;
	if (ion0.n < 0) {
	  ib = ion->iblock[r.upper];
	  if (ib->rec &&
	      ib->rec->nrec[ib->irec] > 10) {
	    continue;
	  }
	}
	rt.f = r.lower;
	j1 = ion->j[r.upper];
	j2 = ion->j[r.lower];
	e = ion->energy[r.upper] - ion->energy[r.lower];
	TRRate(&(rt.dir), &(rt.inv), inv, j1, j2, e, r.strength);
	AddRate(ion, ion->tr_rates, &rt, m);
      }
    }
    fclose(f);
    if (ion0.n < 0.0) continue;
    ExtrapolateTR(ion, inv);
    if (k == 0 && ion0.nionized > 0) {
      f = fopen(ion0.dbfiles[DB_TR-1], "r");
      if (f == NULL) {
	printf("File %s does not exist, skipping.\n", ion0.dbfiles[DB_CE-1]);
	continue;
      }
      n = fread(&fh, sizeof(F_HEADER), 1, f);
      if (CheckEndian(&fh) != endian) {
	swp = 1;
	SwapEndianFHeader(&fh);
      } else {
	swp = 0;
      }
      for (nb = 0; nb < fh.nblocks; nb++) {
	n = fread(&h, sizeof(TR_HEADER), 1, f);
	if (swp) SwapEndianTRHeader(&h);
	if (h.nele != ion0.nele) {
	  fseek(f, h.length, SEEK_CUR);
	  continue;
	}  
	if (abs(h.multipole) == 1) m = 0;
	else m = 1;
	for (i = 0; i < h.ntransitions; i++) {
	  n = fread(&r, sizeof(TR_RECORD), 1, f);
	  if (swp) SwapEndianTRRecord(&r);
	  p = IonizedIndex(r.lower, 0);
	  if (p < 0) {
	    continue;
	  }
	  q = IonizedIndex(r.upper, 0);
	  if (q < 0) {
	    continue;
	  }
	  rt.i = ion0.ionized_map[1][q];
	  rt.f = ion0.ionized_map[1][p];
	  j1 = ion->j[rt.i];
	  j2 = ion->j[rt.f];
	  e = ion0.energy[q] - ion0.energy[p];
	  TRRate(&(rt.dir), &(rt.inv), inv, j1, j2, e, r.strength);
	  AddRate(ion, ion->tr_rates, &rt, m);
	}
      }
      fclose(f);
    }
  }
  return 0;
}

int SetCIRates(int inv) { 
  int nb, i, t;
  int n, m, k;
  int j1, j2;
  int nshells;
  ION *ion;
  RATE rt;
  F_HEADER fh;
  CI_HEADER h;
  CI_RECORD r;
  double e;
  FILE *f;  
  float *params;
  int swp, endian;

  if (ion0.n < 0.0) return 0;
  endian = CheckEndian(NULL);

  if (ion0.atom <= 0) {
    printf("ERROR: Blocks not set, exitting\n");
    exit(1);
  }
  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    ArrayFree(ion->ci_rates, NULL);
    f = fopen(ion->dbfiles[DB_CI-1], "r");
    if (f == NULL) {
      printf("File %s does not exist, skipping.\n", ion->dbfiles[DB_CI-1]);
      continue;
    }
    n = fread(&fh, sizeof(F_HEADER), 1, f);
    if (CheckEndian(&fh) != endian) {
      swp = 1;
      SwapEndianFHeader(&fh);
    } else {
      swp = 0;
    }
    for (nb = 0; nb < fh.nblocks; nb++) {
      n = fread(&h, sizeof(CI_HEADER), 1, f);
      if (swp) SwapEndianCIHeader(&h);
      m = h.n_tegrid + h.n_egrid + h.n_usr;
      fseek(f, sizeof(double)*m, SEEK_CUR);
      m = h.nparams;
      nshells = 1;
      params = (float *) malloc(sizeof(float)*m*nshells);
      for (i = 0; i < h.ntransitions; i++) {
	n = fread(&r, sizeof(CI_RECORD), 1, f);
	if (swp) SwapEndianCIRecord(&r);
	if (r.nshells > nshells) {
	  nshells = r.nshells;
	  params = (float *) realloc(params, sizeof(float)*m*nshells);
	}
	n = fread(params, sizeof(float), m*r.nshells, f);
	if (swp) {
	  for (t = 0; t < m*r.nshells; t++) {
	    SwapEndian((char *) &(params[t]), sizeof(float));
	  }
	}
	fseek(f, sizeof(float)*h.n_usr, SEEK_CUR);
	rt.i = r.b;
	rt.f = r.f;
	j1 = ion->j[r.b];
	j2 = ion->j[r.f];
	e = ion->energy[r.f] - ion->energy[r.b];
	CIRate(&(rt.dir), &(rt.inv), inv, j1, j2, e, m, nshells, params,
	       rt.i, rt.f);
	AddRate(ion, ion->ci_rates, &rt, 0);
      }
      free(params);
    }
    fclose(f);
  }
  return 0;
}

int SetRRRates(int inv) { 
  int nb, i, t;
  int n, m, k;
  int j1, j2;
  int nshells;
  ION *ion;
  RATE rt;
  F_HEADER fh;
  RR_HEADER h;
  RR_RECORD r;
  double e;
  FILE *f;  
  float *params;
  int endian, swp;

  if (ion0.n < 0.0) return 0;
  endian = CheckEndian(NULL);
  if (ion0.atom <= 0) {
    printf("ERROR: Blocks not set, exitting\n");
    exit(1);
  }
  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    ArrayFree(ion->rr_rates, NULL);
    f = fopen(ion->dbfiles[DB_RR-1], "r");
    if (f == NULL) {
      printf("File %s does not exist, skipping.\n", ion->dbfiles[DB_RR-1]);
      continue;
    }
    n = fread(&fh, sizeof(F_HEADER), 1, f);
    if (CheckEndian(&fh) != endian) {
      swp = 1;
      SwapEndianFHeader(&fh);
    } else {
      swp = 0;
    }
    for (nb = 0; nb < fh.nblocks; nb++) {
      n = fread(&h, sizeof(RR_HEADER), 1, f);
      if (swp) SwapEndianRRHeader(&h);
      m = h.n_tegrid + h.n_egrid + h.n_usr;
      fseek(f, sizeof(double)*m, SEEK_CUR);
      m = h.nparams;
      nshells = 1;
      params = (float *) malloc(sizeof(float)*m*nshells);
      for (i = 0; i < h.ntransitions; i++) {
	n = fread(&r, sizeof(RR_RECORD), 1, f);
	if (swp) SwapEndianRRRecord(&r);
	if (r.nshells > nshells) {
	  nshells = r.nshells;
	  params = (float *) realloc(params, sizeof(float)*m*nshells);
	}
	n = fread(params, sizeof(float), m*r.nshells, f);
	if (swp) {
	  for (t = 0; t < m*r.nshells; t++) {
	    SwapEndian((char *) &(params[t]), sizeof(float));
	  }
	}
	  
	fseek(f, sizeof(float)*h.n_usr, SEEK_CUR);
	rt.i = r.f;
	rt.f = r.b;
	j1 = ion->j[r.f];
	j2 = ion->j[r.b];
	e = ion->energy[r.f] - ion->energy[r.b];
	if (e < 0.0) {
	  printf("%d %d %10.3E %10.3E\n", 
		 r.f, r.b, ion->energy[r.f],ion->energy[r.b]);
	  exit(1);
	}
	RRRate(&(rt.dir), &(rt.inv), inv, j1, j2, e, m, nshells, params,
	       rt.i, rt.f);
	AddRate(ion, ion->rr_rates, &rt, 0);
      }
      free(params);
    }
    fclose(f);
    ExtrapolateRR(ion, inv);
  }
  return 0;
}

int SetAIRates(int inv) {
  int nb, i;
  int n, k;
  int j1, j2;
  ION *ion;
  RATE rt;
  F_HEADER fh;
  AI_HEADER h;
  AI_RECORD r;
  double e;
  FILE *f;  
  int swp, endian;

  if (ion0.n < 0.0) return 0;
  endian = CheckEndian(NULL);

  if (ion0.atom <= 0) {
    printf("ERROR: Blocks not set, exitting\n");
    exit(1);
  }
  for (k = 0; k < ions->dim; k++) {
    ion = (ION *) ArrayGet(ions, k);
    ArrayFree(ion->ai_rates, NULL);
    f = fopen(ion->dbfiles[DB_AI-1], "r");
    if (f == NULL) {
      printf("File %s does not exist, skipping.\n", ion->dbfiles[DB_AI-1]);
      continue;
    }
    n = fread(&fh, sizeof(F_HEADER), 1, f);
    if (CheckEndian(&fh) != endian) {
      swp = 1;
      SwapEndianFHeader(&fh);
    } else {
      swp = 0;
    }
    for (nb = 0; nb < fh.nblocks; nb++) {
      n = fread(&h, sizeof(AI_HEADER), 1, f);
      if (swp) SwapEndianAIHeader(&h);
      fseek(f, sizeof(double)*h.n_egrid, SEEK_CUR);
      for (i = 0; i < h.ntransitions; i++) {
	n = fread(&r, sizeof(AI_RECORD), 1, f);
	if (swp) SwapEndianAIRecord(&r);
	rt.i = r.b;
	rt.f = r.f;
	j1 = ion->j[r.b];
	j2 = ion->j[r.f];
	e = ion->energy[r.b] - ion->energy[r.f];
	AIRate(&(rt.dir), &(rt.inv), inv, j1, j2, e, r.rate);
	AddRate(ion, ion->ai_rates, &rt, 0);
      }
    }
    fclose(f);
    ExtrapolateAI(ion, inv);
  }
  return 0;
}