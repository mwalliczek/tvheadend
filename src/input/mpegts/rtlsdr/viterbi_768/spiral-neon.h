#include "build.h"
#if !defined(SPIRAL_NEON_) && defined(CONFIG_NEON)
#define SPIRAL_NEON_
/***************************************************************
This code was generated by Spiral 6.0 beta, www.spiral.net --
Copyright (c) 2005-2008, Carnegie Mellon University.
All rights reserved.
The code is distributed under the GNU General Public License (GPL)
(see http://www.gnu.org/copyleft/gpl.html)

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*AS IS* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************/
#include	<stdint.h>

#define K 7
#define RATE 4
#define POLYS { 109, 79, 83, 109 }
#define NUMSTATES 64
#define FRAMEBITS 2048
#define DECISIONTYPE unsigned int
#define DECISIONTYPE_BITSIZE 32
#define COMPUTETYPE uint32_t
#define EBN0 3
#define TRIALS 10000
#define __int32 int
#define FUNC FULL_SPIRAL
#define METRICSHIFT 0
#define PRECISIONSHIFT 0
#define RENORMALIZE_THRESHOLD 2000000000

void init_FULL_SPIRAL(void);
void FULL_SPIRAL_neon(int amount, int32_t  *Y, int32_t  *X, int32_t  *syms, unsigned char  *dec, int32_t  *Branchtab);

#endif