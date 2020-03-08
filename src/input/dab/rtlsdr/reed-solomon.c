#
/* Initialize a RS codec
 *
 * Copyright 2002 Phil Karn, KA9Q
 * May be used under the terms of the GNU General Public License (GPL)
 *
 *       Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
 *
 */

#include "reed-solomon.h"

#define	min(a,b)	((a) < (b) ? (a) : (b))

galois_t* init_galois(uint16_t symsize, uint16_t gfpoly);
void destroy_galois(galois_t* res);
uint16_t        galois_add_poly (uint16_t a, uint16_t b);
uint16_t galois_poly2power(galois_t* galois, uint16_t a);
uint16_t galois_power2poly(galois_t* galois, uint16_t a);
int     galois_modnn (galois_t* galois, int x);
uint16_t galois_multiply_power  (galois_t* galois, uint16_t a, uint16_t b);
uint16_t galois_divide_power (galois_t* galois, uint16_t a, uint16_t b);
uint16_t galois_divide_poly (galois_t* galois, uint16_t a, uint16_t b);
uint16_t galois_multiply_poly (galois_t* galois, uint16_t a, uint16_t b);
uint16_t galois_pow_power (galois_t* galois, uint16_t a, uint16_t n);

int16_t reedSolomon_decode_rs (reedSolomon_t* res, uint8_t *data);
uint8_t	reedSolomon_getSyndrome (reedSolomon_t* res, uint8_t *data, uint8_t root);
int	reedSolomon_computeSyndromes (reedSolomon_t* res, uint8_t *data, uint8_t *syndromes);
uint16_t reedSolomon_computeLambda (reedSolomon_t* res, uint8_t *syndromes, uint8_t *Lambda);
int16_t  reedSolomon_computeErrors (reedSolomon_t* res, uint8_t *Lambda,
	                             uint16_t deg_lambda,
	                             uint8_t *rootTable,
	                             uint8_t *locTable);
uint16_t reedSolomon_computeOmega (reedSolomon_t* res, uint8_t *syndromes,
	                            uint8_t *lambda, uint16_t deg_lambda,
	                            uint8_t *omega);

galois_t* init_galois(uint16_t symsize, uint16_t gfpoly) {
galois_t* res = calloc(1, sizeof(galois_t));

uint16_t sr;
uint16_t i;

	res	-> mm		= symsize;
	res	-> gfpoly	= gfpoly;
	res	-> codeLength	= (1 << res->mm) - 1;
	res	-> d_q		= 1 << res->mm;
	res	-> alpha_to	= calloc(res->codeLength + 1, sizeof(uint16_t));
	res	-> index_of	= calloc(res->codeLength + 1, sizeof(uint16_t));
/*	Generate Galois field lookup tables */
	res->index_of [0] = res->codeLength;	/* log (zero) = -inf */
	res->alpha_to [res->codeLength] = 0;	/* alpha**-inf = 0 */

	sr = 1;
	for (i = 0; i < res->codeLength; i++){
	   res->index_of [sr] = i;
	   res->alpha_to [i] = sr;
	   sr <<= 1;
	   if (sr & (1 << symsize))
	      sr ^= gfpoly;
	   sr &= res->codeLength;
	}
	
	return res;
}

void destroy_galois(galois_t* res) {
	free(res->alpha_to);
	free(res->index_of);
	free(res);
}

uint16_t	galois_add_poly	(uint16_t a, uint16_t b) {
	return a ^ b;
}

uint16_t galois_poly2power(galois_t* galois, uint16_t a) {
	return galois->index_of [a];
}

uint16_t galois_power2poly(galois_t* galois, uint16_t a) {
	return galois->alpha_to [a];
}

int	galois_modnn (galois_t* galois, int x){
	while (x >= galois->codeLength) {
	   x -= galois->codeLength;
	   x = (x >> galois->mm) + (x & galois->codeLength);
	}
	return x;
}


uint16_t galois_multiply_power	(galois_t* galois, uint16_t a, uint16_t b) {
	return galois_modnn (galois, a + b);
}

uint16_t galois_multiply_poly (galois_t* galois, uint16_t a, uint16_t b) {
	if ((a == 0) || (b == 0))
	   return 0;
	return galois->alpha_to [galois_multiply_power (galois, galois->index_of [a], galois->index_of [b])];
}

uint16_t galois_divide_power (galois_t* galois, uint16_t a, uint16_t b) {
	return galois_modnn (galois, galois->d_q - 1 + a - b);
}

uint16_t galois_divide_poly (galois_t* galois, uint16_t a, uint16_t b) {
	if (a == 0)
	   return 0;
	return galois->alpha_to [galois_divide_power (galois, galois->index_of [a], galois->index_of [b])];
}

uint16_t galois_pow_power (galois_t* galois, uint16_t a, uint16_t n) {
	return (a == 0) ? 0 : (a * n) % (galois->d_q - 1);
}

reedSolomon_t*  init_reedSolomon(uint16_t symsize,
	                          uint16_t gfpoly,
	                          uint16_t fcr,
	                          uint16_t prim,
	                          uint16_t nroots) {

reedSolomon_t* res = calloc(1, sizeof(reedSolomon_t));
int i, j, root, iprim;

	res->myGalois = init_galois(symsize, gfpoly);
	
	res	-> symsize 	= symsize;		// in bits
	res	-> codeLength	= (1 << symsize) - 1;
	res	-> fcr		= fcr;
	res	-> prim		= prim;
	res	-> nroots	= nroots;
	for (iprim = 1; (iprim % prim) != 0; iprim += res->codeLength);
	res -> iprim = iprim / prim;
	res	-> generator	= calloc(nroots + 1, sizeof(uint8_t));
	memset (res->generator, 0, (nroots + 1) * sizeof (res->generator [0]));
	res->generator [0] = 1;

	for (i = 0, root = fcr * prim; i < nroots; i++, root += 1) {
	   res->generator [i + 1] = 1;
	   for (j = i; j > 0; j--){
	      if (res->generator [j] != 0) {
	         uint16_t p1 = galois_multiply_power (res->myGalois,
	                                   galois_poly2power (res->myGalois, res->generator [j]),
	                                   root);
	         res->generator [j] = galois_add_poly (
	                res->generator [j - 1],
	                galois_power2poly (res->myGalois, p1));
	         
	      }
	      else {
	         res->generator [j] = res->generator [j - 1];
	      }
	   }

/*	rsHandle -> genpoly [0] can never be zero */
	   res->generator [0] =
	           galois_power2poly (res->myGalois,
	                galois_multiply_power (res->myGalois, root,
				  galois_poly2power (res->myGalois, res->generator [0])));
	}
	for (i = 0; i <= nroots; i ++)
	   res->generator [i] = galois_poly2power (res->myGalois, res->generator [i]);
	
	
	return res;	                        
}

void destroy_reedSolomon(reedSolomon_t* res) {
	destroy_galois(res->myGalois);
	free(res->generator);
	free(res);
}

int16_t	reedSolomon_decode_rs (reedSolomon_t* res, uint8_t *data) {
uint8_t syndromes [res->nroots];
uint8_t Lambda	  [res->nroots];
uint16_t lambda_degree, omega_degree;
uint8_t	rootTable [res->nroots];
uint8_t	locTable  [res->nroots];
uint8_t	omega	  [res->nroots];
int16_t	rootCount;
int16_t	i;
//
//	returning syndromes in poly
	if (reedSolomon_computeSyndromes (res, data, syndromes))
	   return 0;
//	Step 2: Berlekamp-Massey
//	Lambda in power notation
	lambda_degree = reedSolomon_computeLambda (res, syndromes, Lambda);

//	Step 3: evaluate lambda and compute the error locations (chien)
	rootCount = reedSolomon_computeErrors (res, Lambda, lambda_degree, rootTable, locTable);
	if (rootCount < 0)
	   return -1;
	omega_degree = reedSolomon_computeOmega (res, syndromes, Lambda, lambda_degree, omega);
/*
 *	Compute error values in poly-form.
 *	num1 = omega (inv (X (l))),
 *	num2 = inv (X (l))**(FCR-1) and
 *	den = lambda_pr(inv(X(l))) all in poly-form
 */
	uint16_t num1, num2, den;
	int16_t j;
	for (j = rootCount - 1; j >= 0; j--) {
	   num1 = 0;
	   for (i = omega_degree; i >= 0; i--) {
	      if (omega [i] != res->codeLength) {
	         uint16_t tmp = galois_multiply_power (res->myGalois, omega [i],
	                           galois_pow_power (res->myGalois, i, rootTable [j]));
	         num1	= galois_add_poly (num1, 
	                              galois_power2poly (res->myGalois, tmp));
	      }
	   }
	   uint16_t tmp = galois_multiply_power (res->myGalois,
	                              galois_multiply_power (res->myGalois,
	                                     rootTable [j],
	                                     galois_divide_power (res->myGalois, res->fcr, 1)),
	                              res->codeLength);
	   num2	= galois_power2poly (res->myGalois, tmp);
	   den = 0;
/*
 *	lambda [i + 1] for i even is the formal derivative
 *	lambda_pr of lambda [i]
 */
	   for (i = min (lambda_degree, res->nroots - 1) & ~1;
	                 i >= 0; i -=2) {
	      if (Lambda [i + 1] != res->codeLength) {
	         uint16_t tmp = galois_multiply_power (res->myGalois, Lambda [i + 1],
	                             galois_pow_power (res->myGalois, i, rootTable [j]));
	         den	= galois_add_poly (den, galois_power2poly (res->myGalois, tmp));
	      }
	   }

	   if (den == 0) {
//	      fprintf (stderr, "den = 0, (count was %d)\n", den);
	      return -1;
	   }
/*	Apply error to data */
	   if (num1 != 0) {
	      if (locTable [j] >= (res->codeLength - res->nroots))
	         rootCount --;
	      else {
	         uint16_t tmp1	= res->codeLength - galois_poly2power (res->myGalois, den);
	         uint16_t tmp2	= galois_multiply_power (res->myGalois,
	                               galois_poly2power (res->myGalois, num1),
	                               galois_poly2power (res->myGalois, num2));
	         tmp2		= galois_multiply_power (res->myGalois, tmp2, tmp1);
	         uint16_t corr	= galois_power2poly (res->myGalois, tmp2);
	         data [locTable [j]] =
	                         galois_add_poly (data [locTable [j]], corr);
	      }
 	   }
	
 	}
	return rootCount;
}


int16_t reedSolomon_dec (reedSolomon_t* res, const uint8_t *r, uint8_t *d, int16_t cutlen) {
uint8_t rf [res->codeLength];
int16_t i;
int16_t	ret;

	memset (rf, 0, cutlen * sizeof (rf [0]));
	for (i = cutlen; i < res->codeLength; i++)
	   rf [i] = r [i - cutlen];

	ret = reedSolomon_decode_rs (res, rf);
	for (i = cutlen; i < res->codeLength - res->nroots; i++)
	   d [i - cutlen] = rf [i];
	return ret;
}

//
//	Apply Horner on the input for root "root"
uint8_t	reedSolomon_getSyndrome (reedSolomon_t* res, uint8_t *data, uint8_t root) {
uint8_t	syn	= data [0];
int16_t j;

	for (j = 1; j < res->codeLength; j++){
	   if (syn == 0)
	      syn = data [j];
	   else {
	      uint16_t uu1 = galois_pow_power(res->myGalois,
	                           galois_multiply_power (res->myGalois, res->fcr, root),
	                           res->prim);
	      syn = galois_add_poly (data [j],
	                      galois_power2poly (res->myGalois,
	                           galois_multiply_power (res->myGalois,
	                               galois_poly2power (res->myGalois, syn), uu1)));
//	                                                   (fcr + root) * prim)));
	   }
	}
	return syn;
}

//
//	use Horner to compute the syndromes
int	reedSolomon_computeSyndromes (reedSolomon_t* res, uint8_t *data, uint8_t *syndromes) {
int16_t i;
uint16_t syn_error = 0;

/* form the syndromes; i.e., evaluate data (x) at roots of g(x) */

	for (i = 0; i < res->nroots; i++) {
	   syndromes [i] = reedSolomon_getSyndrome (res, data, i);
	   syn_error |= syndromes [i];
	}

	return syn_error == 0;
}
//
//	compute Lambda with Berlekamp-Massey
//	syndromes in poly-form in, Lambda in power form out
//	
uint16_t reedSolomon_computeLambda (reedSolomon_t* res, uint8_t *syndromes, uint8_t *Lambda) {
uint16_t K = 1, L = 0;
uint8_t Corrector	[res->nroots];
int16_t  i;
int16_t	deg_lambda	= 0;

	for (i = 0; i < res->nroots; i ++)
	   Corrector [i] = Lambda [i] = 0;

	uint8_t	error	= syndromes [0];
//
//	Initializers: 
	Lambda	[0]	= 1;
	Corrector [1]	= 1;
//
	while (K <= res->nroots) {
	   uint8_t oldLambda [res->nroots];
	   memcpy (oldLambda, Lambda, res->nroots * sizeof (Lambda [0]));
//
//	Compute new lambda
	   for (i = 0; i < res->nroots; i ++) 
	      Lambda [i] = galois_add_poly (Lambda [i],
	                             galois_multiply_poly (res->myGalois, error, 
	                                                      Corrector [i]));
	   if ((2 * L < K) && (error != 0)) {
	      L = K - L;
	      for (i = 0; i < res->nroots; i ++) 
	         Corrector [i] = galois_divide_poly (res->myGalois, oldLambda [i], error);
	   }
//
//	multiply x * C (x), i.e. shift to the right, the 0-th order term is left
	   for (i = res->nroots - 1; i >= 1; i --)
	      Corrector [i] = Corrector [i - 1];
	   Corrector [0] = 0;

//	and compute a new error
	   error	= syndromes [K];	
	   for (i = 1; i <= K; i ++)  {
	      error = galois_add_poly (error, galois_multiply_poly (res->myGalois, syndromes [K - i],
	                                                     Lambda [i]));
	   }
	   K += 1;
 	} // end of Berlekamp loop

	for (i = 0; i < res->nroots; i ++) {
	   if (Lambda [i] != 0)
	      deg_lambda = i;
	   Lambda [i] = galois_poly2power (res->myGalois, Lambda [i]);
	}
	return deg_lambda;
}
//
//	Compute the roots of lambda by evaluating the
//	lambda polynome for all (inverted) powers of the symbols
//	of the data (Chien search)
int16_t  reedSolomon_computeErrors (reedSolomon_t* res, uint8_t *Lambda,
	                             uint16_t deg_lambda,
	                             uint8_t *rootTable,
	                             uint8_t *locTable) {
int16_t i, j, k;
int16_t rootCount = 0;
//
	uint8_t workRegister [res->nroots + 1];
	memcpy (&workRegister, Lambda, (res->nroots + 1) * sizeof (uint8_t));
//
//	reg is lambda in power notation
	for (i = 1, k = res->iprim - 1;
	     i <= res->codeLength; i ++, k = (k + res->iprim)) {
	   uint16_t result = 1;	// lambda [0] is always 1
//	Note that for i + 1, the powers in the workregister just need
//	to be increased by "j".
	   for (j = deg_lambda; j > 0; j --) {
	      if (workRegister [j] != res->codeLength)  {
	         workRegister [j] = galois_multiply_power (res->myGalois, workRegister [j],
	                                                      j);
	         result = galois_add_poly (result,
	                                       galois_power2poly
	                                              (res->myGalois, workRegister [j]));
	      }
	   }
	   if (result != 0)		// no root
	      continue;
	   rootTable [rootCount] = i;
	   locTable  [rootCount] = k;
	   rootCount ++;
	}
	if (rootCount != deg_lambda)
	   return -1;
	return rootCount;
}

/*
 *	Compute error evaluator poly
 *	omega(x) = s(x)*lambda(x) (modulo x**NROOTS)
 *	in power form, and  find degree (omega).
 *
 *	Note that syndromes are in poly form, while lambda in power form
 */
uint16_t reedSolomon_computeOmega (reedSolomon_t* rs, uint8_t *syndromes,
	                            uint8_t *lambda, uint16_t deg_lambda,
	                            uint8_t *omega) {
int16_t i, j;
int16_t	deg_omega = 0;

	for (i = 0; i < rs->nroots; i++){
	   uint16_t tmp = 0;
	   j = (deg_lambda < i) ? deg_lambda : i;
	   for (; j >= 0; j--){
	      if ((galois_poly2power (rs->myGalois, syndromes [i - j]) != rs->codeLength) &&
	          (lambda [j] != rs->codeLength)) {
	         uint16_t res = galois_power2poly (rs->myGalois,
	                             galois_multiply_power (rs->myGalois,
	                                           galois_poly2power (rs->myGalois,
	                                                 syndromes [i - j]), 
	                                           lambda [j]));
	         tmp =  galois_add_poly (tmp, res);
	      }
	   }

	   if (tmp != 0)
	      deg_omega = i;
	   omega [i] = galois_poly2power (rs->myGalois, tmp);
	}

	omega [rs->nroots] = rs->codeLength;
	return deg_omega;
}

