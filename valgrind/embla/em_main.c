
/*--------------------------------------------------------------------*/
/*--- embla: A dependency profiler.                      em_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Embla, a Valgrind tool for dependency profiling.

   Copyright (C) 2005- The Embla team
      kff@sics.se

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

/* 
   Things that should be changed include, but are not limited to, the following:
   - There is an assumption throughout that we run code for a 32 bit machine.

*/

#include "pub_tool_basics.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_tooliface.h"
#include "libvex_guest_offsets.h"
#include "pub_tool_options.h"

static unsigned int translations = 0;
static unsigned int executions = 0;
static unsigned int exits_found = 0;
static unsigned int call_ret_found = 0;

typedef 
   enum {
     DIC_Call,
     DIC_Ret,
     DIC_Other
   } 
   DInstrClass;

typedef
   struct _StackFrame {
     Addr32 pc,sp;
     unsigned int seq;
     struct _StackFrame *parent;
   }
   StackFrame;

StackFrame * current_stack_frame = NULL;

Char buf[512];
int first_sp = 0;

int trace_file_fd;

DInstrClass last_imark_class = DIC_Other;

static void em_post_clo_init(void)
{
   SysRes sres;

   sres = VG_(open)((Char *) "embla.trace", 
                    VKI_O_CREAT | VKI_O_TRUNC | VKI_O_WRONLY,
                    VKI_S_IRUSR | VKI_S_IWUSR);

   if( sres.isError ) {
      VG_(message)(Vg_UserMsg, "Trace file could not be opened");
      return;
   }

   trace_file_fd = sres.val;

   VG_(clo_vex_control).iropt_unroll_thresh = 0;
   VG_(clo_vex_control).guest_chase_thresh = 0;


}

static DInstrClass decodeGuestInstr(Addr32 addr)
{
    UChar *ip = (UChar *) addr;

    // Skip prefixes
    switch( *ip ) {
        case 0xf0: // LOCK prefix
        case 0xf2: // rep prefix
        case 0xf3: // rep prefix
            ip++;
            break;
        default:
            break;
    }
    switch( *ip ) {
        case 0x67: // Address size override
            ip++;
            break;
        default:   // Not a prefix
            break;
    }
    switch( *ip ) {
        case 0x66: // Operand size override
            ip++;
            break;
        default:
            break;
    }
    switch( *ip ) {
        case 0x2e: // CS segment override
        case 0x36: // SS segment override
        case 0x3e: // DS segment override
        case 0x26: // ES segment override
        case 0x64: // FS segment override
        case 0x65: // GS segment override
            ip++;
            break;
        default:
            break;
    }

    // Now look at the opcode
    switch( *ip ) {
        case 0xe8: // A pc relative CALL
            return DIC_Call;
        case 0xff: // Depends on the Mod/RM byte
            ip++;
            if( (((*ip)>>3)&7) == 2 ) {
                return DIC_Call;
            }
            break;
        case 0xc2:
        case 0xc3:
            return DIC_Ret;
    }
    return DIC_Other;
}

static VG_REGPARM(2)
void recordCall(int sp, int ra) 
{
    StackFrame * newFrame = (StackFrame *) VG_(calloc)(1,sizeof(StackFrame));

    newFrame->parent = current_stack_frame;
    newFrame->sp = sp;
    newFrame->pc = ra;
    newFrame->seq = 0;

    current_stack_frame = newFrame;

}

static VG_REGPARM(0)
void recordRet(void) 
{
    

}










char *hexdigits = "0123456789abcdef";

static VG_REGPARM(3)
void recordCallOrReturn(int sp, int pc, int len)
{
    HChar *str;
    unsigned char *spc = (unsigned char *) pc;
    int i;
    DInstrClass ic = decodeGuestInstr((Addr32) pc);

    if( first_sp == 0 ) {
       first_sp = sp;
    }

    if( ic == DIC_Call ) {
       str = (HChar *) "%u %d Call";
    } else if( ic == DIC_Ret ) {
       str = (HChar *) "%u %d Ret";
    } else {
       str = (HChar *) "%u %d -";
    }

    VG_(sprintf)(buf, str, pc, first_sp - sp);
    VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

    for( i=0; i<len; i++ ) {
       buf[0]=' ';
       buf[1]=hexdigits[(*spc)>>4];
       buf[2]=hexdigits[(*spc)&15];
       buf[3]=0;
       VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));
       spc++;
    }
    VG_(sprintf)(buf, "\n");
    VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

}


static VG_REGPARM(3)
void recordExit(IRJumpKind jk, DInstrClass ic, int pc)
{
    HChar *str;

    if( ic == DIC_Call ) {
       str = (HChar *) "%u DIC_Call ";
    } else if( ic == DIC_Ret ) {
       str = (HChar *) "%u DIC_Ret  ";
    } else {
       str = (HChar *) "%u DIC_Other";
    }

    VG_(sprintf)(buf, str, pc);
    VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

    if( jk == Ijk_Call ) {
       str = (HChar *) " Ijk_Call\n";
    } else if( jk == Ijk_Ret ) {
       str = (HChar *) " Ijk_Ret\n";
    } else {
       str = (HChar *) " Ijk_Other\n";
    }

    VG_(sprintf)(buf, str, pc);
    VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

}

static VG_REGPARM(0)
void recordEntry()
{
    VG_(sprintf)(buf, "Block entered\n");
    VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));
}


static void instrumentCallOrReturn(IRBB *bbOut, DInstrClass jt, Addr32 pc, Int len)
{
    IRTemp sp_val = newIRTemp(bbOut->tyenv, Ity_I32);
    HChar *hname = "recordCallOrReturn";
    void  *haddr = &recordCallOrReturn;
    IRExpr **args = mkIRExprVec_3( IRExpr_Tmp(sp_val),
                                   IRExpr_Const( IRConst_U32( pc ) ),
                                   IRExpr_Const( IRConst_U32( len ) ) );
    IRDirty *dy = unsafeIRDirty_0_N( 3, hname, haddr, args );
    IRExpr  *get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );

    addStmtToIRBB( bbOut, IRStmt_Tmp( sp_val, get_sp ) );
    addStmtToIRBB( bbOut, IRStmt_Dirty(dy) );
}

static void instrumentExit(IRBB *bbOut, IRJumpKind jk, Addr32 pc)
{
    HChar *hname = "recordExit";
    void  *haddr = &recordExit;
    DInstrClass ic = decodeGuestInstr( pc );
    IRExpr **args = mkIRExprVec_3( IRExpr_Const( IRConst_U32( jk ) ),
                                   IRExpr_Const( IRConst_U32( ic ) ),
                                   IRExpr_Const( IRConst_U32( pc ) ) );
    IRDirty *dy = unsafeIRDirty_0_N( 3, hname, haddr, args );

    addStmtToIRBB( bbOut, IRStmt_Dirty(dy) );
}

static void instrumentBBentry(IRBB *bbOut)
{
    IRDirty *dy = unsafeIRDirty_0_N( 0, "recordEntry", &recordEntry, mkIRExprVec_0() );
    addStmtToIRBB( bbOut, IRStmt_Dirty( dy ) );
}

static IRBB* em_instrument(IRBB* bbIn, VexGuestLayout* layout, 
                           IRType gWordTy, IRType hWordTy)
{
    IRBB      *bbOut;
    IRTemp    t1,t2;
    IRExpr    *the_addr, *one;
    int       bbIn_idx;
    IRStmt    *stIn;
    Addr32    guestIAddr = 0;
    Int       guestILen  = 0;
    DInstrClass  ic;

    translations++;

    bbOut           = emptyIRBB();                     // from libvex_ir.h
    bbOut->tyenv    = dopyIRTypeEnv(bbIn->tyenv);      // d:o
    bbOut->next     = dopyIRExpr(bbIn->next);          // d:o
    bbOut->jumpkind = bbIn->jumpkind;

    t1 = newIRTemp(bbOut->tyenv, Ity_I32);
    t2 = newIRTemp(bbOut->tyenv, Ity_I32);

    the_addr = IRExpr_Const( IRConst_U32( (UInt) &executions ) );
    one      = IRExpr_Const( IRConst_U32( 1 ) );

/*
    addStmtToIRBB( bbOut, IRStmt_Tmp(t1, IRExpr_Load(Iend_LE, Ity_I32, the_addr)) );
    addStmtToIRBB( bbOut, IRStmt_Tmp(t2, IRExpr_Binop(Iop_Add32, IRExpr_Tmp(t1), one)) );
    addStmtToIRBB( bbOut, IRStmt_Store(Iend_LE, the_addr, IRExpr_Tmp(t2)) );
*/

    // instrumentBBentry( bbOut );

    for( bbIn_idx = 0; bbIn_idx < bbIn->stmts_used; bbIn_idx++ ) {
       stIn = bbIn->stmts[bbIn_idx];
       
       switch( stIn->tag ) {

         case Ist_IMark: 
             guestIAddr = (Addr32) stIn->Ist.IMark.addr;
             guestILen  = stIn->Ist.IMark.len;
             ic = decodeGuestInstr(guestIAddr);
             last_imark_class = ic;
             if(1 || ic != DIC_Other ) {
                 instrumentCallOrReturn( bbOut, ic, guestIAddr, guestILen );
             }
             break;

         /* Here we look at all jumps, even if they are due to a repeat, but
            then they should not have kind Call or Ret.
         */
         case Ist_Exit:
             if( stIn->Ist.Exit.jk == Ijk_Call || stIn->Ist.Exit.jk == Ijk_Ret ) {
               // instrumentExit( bbOut, stIn->Ist.Exit.jk, guestIAddr );
               ic = decodeGuestInstr(guestIAddr);
               instrumentCallOrReturn( bbOut, ic, guestIAddr, guestILen );
             }
             break;

         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_Tmp:
         case Ist_Store:
         case Ist_Dirty:
         case Ist_MFence:
             break;

       }
       addStmtToIRBB( bbOut, stIn );
    }

    
    if( bbIn->jumpkind == Ijk_Call || bbIn->jumpkind == Ijk_Ret ) {
      // instrumentExit( bbOut, bbIn->jumpkind, guestIAddr );
      ic = decodeGuestInstr(guestIAddr);
      instrumentCallOrReturn( bbOut, ic, guestIAddr, guestILen );
    }
    

    return bbOut;
}

static void em_fini(Int exitcode)
{

   VG_(sprintf)(buf, "Exits: %d, Calls and returns: %d\n", exits_found, call_ret_found);
   
   // VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

   VG_(close)(trace_file_fd);

}

static void em_pre_clo_init(void)
{
   VG_(details_name)            ((Char *) "Embla");
   VG_(details_version)         (NULL);
   VG_(details_description)     ((Char *) "a dependency profiler");
   VG_(details_copyright_author)(
      (Char *) "Copyright (C) 2005, and GNU GPL'd, by the Embla team.");
   VG_(details_bug_reports_to)  ((Char *) "kff@sics.se");

   VG_(basic_tool_funcs)        (em_post_clo_init,
                                 em_instrument,
                                 em_fini);

   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(em_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
