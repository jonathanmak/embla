
/*
   ----------------------------------------------------------------

   Notice that the following BSD-style license applies to this one
   file (memcheck.h) only.  The rest of Valgrind is licensed under the
   terms of the GNU General Public License, version 2, unless
   otherwise indicated.  See the CBINARYYING file in the source
   distribution for details.

   ----------------------------------------------------------------

   This file is part of Embla, a dependence profiler

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

   2. The origin of this software must not be misrepresented; you must 
      not claim that you wrote the original software.  If you use this 
      software in a product, an acknowledgment in the product 
      documentation would be appreciated but is not required.

   3. Altered source versions must be plainly marked as such, and must
      not be misrepresented as being the original software.

   4. The name of the author may not be used to endorse or promote 
      products derived from this software without specific prior written 
      permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
   OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   ----------------------------------------------------------------

   Notice that the above BSD-style license applies to this one file
   (memcheck.h) only.  The entire rest of Valgrind is licensed under
   the terms of the GNU General Public License, version 2.  See the
   CBINARYYING file in the source distribution for details.

   ---------------------------------------------------------------- 
*/


#ifndef __EMBLA_H
#define __EMBLA_H


/* This file is for inclusion into client (your!) code.

   You can use these macros to manipulate and query memory permissions
   inside your own programs.

   See comment near the top of valgrind.h on how to use them.
*/

#include "valgrind.h"

/* !! ABIWARNING !! ABIWARNING !! ABIWARNING !! ABIWARNING !! 
   This enum comprises an ABI exported by Valgrind to programs
   which use client requests.  DO NOT CHANGE THE ORDER OF THESE
   ENTRIES, NOR DELETE ANY -- add new ones at the end. */
typedef
   enum { 
      VG_USERREQ__REDUCTION_BEGIN = VG_USERREQ_TOOL_BASE('E','M'),
      VG_USERREQ__REDUCTION_END,
      /* This is just for embla's internal use - don't use it */
      _VG_USERREQ__EMBLA_RECORD_OVERLAP_ERROR = VG_USERREQ_TOOL_BASE('E','M') + 256
   } Vg_EmblaClientRequest;



/* Client-code macros to manipulate the state of memory. */

#define __REDUCTION_UNARY(op) __extension__                      \
  ({unsigned int _qzz_res;                                       \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* default return */, \
        VG_USERREQ__REDUCTION_BEGIN,                             \
        0, 0, 0, 0, 0);                                          \
    op;                                                          \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* default return */, \
        VG_USERREQ__REDUCTION_END,                               \
        0, 0, 0, 0, 0);                                          \
   })

#define __REDUCTION_BINARY(var, op, arg) __extension__     \
  ({register typeof(var) __red_temp = (arg);                            \
    unsigned int _qzz_res;                                       \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* default return */, \
        VG_USERREQ__REDUCTION_BEGIN,                             \
        0, 0, 0, 0, 0);                                          \
    (var) op __red_temp;                                         \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* default return */, \
        VG_USERREQ__REDUCTION_END,                               \
        0, 0, 0, 0, 0);                                          \
   })

#define __RED_PLUSPLUS(var) __REDUCTION_UNARY((var)++)
#define __RED_MINUSMINUS(var) __REDUCTION_UNARY((var)--)
#define __RED_PLUS(var, arg) __REDUCTION_BINARY(var, +=, arg)
#define __RED_TIMES(var, arg) __REDUCTION_BINARY(var, *=, arg)
#define __RED_MINUS(var, arg) __REDUCTION_BINARY(var, -=, arg)
#define __RED_AND(var, arg) __REDUCTION_BINARY(var, =(var)&&, arg)
#define __RED_OR(var, arg) __REDUCTION_BINARY(var, =(var)||, arg)
#define __RED_BAND(var, arg) __REDUCTION_BINARY(var, &=, arg)
#define __RED_BXOR(var, arg) __REDUCTION_BINARY(var, ^=, arg)
#define __RED_BOR(var, arg) __REDUCTION_BINARY(var, |=, arg)
#define __RED_ASSIGN(var, arg) __REDUCTION_BINARY(var, =, arg)

#endif /* __EMBLA_H */

