
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
#include "pub_tool_options.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_libcassert.h"

#include "libvex_guest_offsets.h"

static unsigned int translations = 0;
static unsigned int instructions = 0;

typedef
   struct _StackFrame {
     Addr32 sp,call_addr,ret_addr;
     unsigned int seq;
     struct _StackFrame *parent;
   }
   StackFrame;

static StackFrame first_stack_frame = {(unsigned int) -1, 0, 0, 0, NULL};

static StackFrame * current_stack_frame = &first_stack_frame;

static Char buf[512];
static int first_sp = 0;

static int trace_file_fd;


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

static VG_REGPARM(3)
void recordCall(int sp, int ca, int ra) 
{
    StackFrame * newFrame = (StackFrame *) VG_(calloc)(1,sizeof(StackFrame));

    newFrame->parent    = current_stack_frame;
    newFrame->sp        = sp;
    newFrame->call_addr = ca;
    newFrame->ret_addr  = ra;
    newFrame->seq       = instructions;

    current_stack_frame = newFrame;

    // some sprintf debugging ...
    VG_(sprintf)(buf, "Call %u %u %u\n", sp, ca, ra);
    VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

}

static VG_REGPARM(2)
void recordRet(int sp, int target) 
{
   // Should check that we are really returning to the next stack frame
   // and in that case make it the current stack frame
   // It should NOT deallocate the current stack frame, though
   
   StackFrame   *current = current_stack_frame;
   unsigned int stack_sp;
   unsigned int stack_ra;
   StackFrame    *parent;
   int           unwind=0;

   // some sprintf debugging ...
   VG_(sprintf)(buf, "Return ");
   VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

   if( current == NULL ) return;

   parent = current->parent;

   // the sp in the StackFrame struct is the sp *after* pushing the 
   // return address; hence adding 4 bytes should yield the sp 
   // after the return has popped

   while( parent != NULL && parent->sp+4 < sp ) {
     // we need to unwind the stack
     current=parent;
     parent=parent->parent;
     unwind++;
   }
   // we have found the right stack frame
   stack_sp = current->sp;
   stack_ra = current->ret_addr;

   // some sprintf debugging ...
   VG_(sprintf)(buf, "%d ", unwind);
   VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

   if( current->sp+4 > sp ) {
     // this return does not correspond to the appropriate call
     // do nothing
     // some sprintf debugging ...
     VG_(sprintf)(buf, "to same ");
     VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));

     current_stack_frame=current;
   } else {
     // we have returned and possibly popped a few more words off of the stack
     if( current->ret_addr != target ) {
       // we're not returning to the scene of the crime, sorry, call
       
       // some sprintf debugging ...
       VG_(sprintf)(buf, "but funny ");
       VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));
       
     }
     // we will not need this retrun address any more
     current_stack_frame=parent;
   }

   // some sprintf debugging ...
   VG_(sprintf)(buf, "\n");
   VG_(write)(trace_file_fd, (void*)buf, VG_(strlen)(buf));


}


// untility function for emitting code to increment a global variable

static void emitIncrementGlobal(IRBB *bbOut, void *addr, unsigned int amount)
{
    IRTemp tmp_instrs_old = newIRTemp( bbOut->tyenv, Ity_I32 );
    IRTemp tmp_instrs_new = newIRTemp( bbOut->tyenv, Ity_I32 );
    IRExpr *exp_instrs_new = IRExpr_Tmp( tmp_instrs_new );

    IRExpr *exp_instr_addr = IRExpr_Const( IRConst_U32( (UInt) addr ) );
    IRExpr *exp_instrs = IRExpr_Load( Iend_LE, Ity_I32, exp_instr_addr );
    IRExpr *exp_amount = IRExpr_Const( IRConst_U32( amount ) );
    IRExpr *exp_add = IRExpr_Binop( Iop_Add32, IRExpr_Tmp( tmp_instrs_old ), exp_amount );

    addStmtToIRBB( bbOut, IRStmt_Tmp( tmp_instrs_old, exp_instrs ) );
    addStmtToIRBB( bbOut, IRStmt_Tmp( tmp_instrs_new, exp_add ) );
    addStmtToIRBB( bbOut, IRStmt_Store( Iend_LE, exp_instr_addr, exp_instrs_new ) );
}


// instrumentExit is called when we find and Exit in the block AND for the
// implicit Exit at the end of each block

static void instrumentExit(IRBB *bbOut, IRJumpKind jk, Addr32 pc, Int len, IRExpr *tgt,
                           unsigned int loc_instr)
{
    switch( jk ) {

      case Ijk_Call: 
          {
             HChar *hname = "recordCall";
             void  *haddr = &recordCall;
             IRTemp tmp_sp = newIRTemp(bbOut->tyenv, Ity_I32);
             IRExpr *exp_sp = IRExpr_Tmp( tmp_sp );
             IRExpr *exp_ca = IRExpr_Const( IRConst_U32( pc ) );
             IRExpr *exp_ra = IRExpr_Const( IRConst_U32( pc+len ) );
             IRExpr *exp_get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );
             IRExpr **args = mkIRExprVec_3( exp_sp, exp_ca, exp_ra );
             IRDirty *dy = unsafeIRDirty_0_N( 3, hname, haddr, args );
             
             addStmtToIRBB( bbOut, IRStmt_Tmp( tmp_sp, exp_get_sp ) );
             addStmtToIRBB( bbOut, IRStmt_Dirty(dy) );
          }
          break;

      case Ijk_Ret:
          {  
             if( tgt==NULL ) {
                VG_(tool_panic)("Ret not at end of BB!");
             }
             HChar *hname = "recordRet";
             void  *haddr = &recordRet;
             IRTemp tmp_sp = newIRTemp(bbOut->tyenv, Ity_I32);
             IRTemp tmp_pc = newIRTemp(bbOut->tyenv, Ity_I32);
             IRExpr *exp_sp = IRExpr_Tmp( tmp_sp );
             IRExpr *exp_pc = IRExpr_Tmp( tmp_pc );
             IRExpr *exp_get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );
             IRExpr **args = mkIRExprVec_2( exp_sp, exp_pc );
             IRDirty *dy = unsafeIRDirty_0_N( 2, hname, haddr, args );
             
             addStmtToIRBB( bbOut, IRStmt_Tmp( tmp_sp, exp_get_sp ) );
             addStmtToIRBB( bbOut, IRStmt_Tmp( tmp_pc, tgt ) );
             addStmtToIRBB( bbOut, IRStmt_Dirty(dy) );
          }
          break;

      default: 
          // Do nothing
          break;
    }
}

static IRBB* em_instrument(IRBB* bbIn, VexGuestLayout* layout, 
                           IRType gWordTy, IRType hWordTy)
{
    IRBB      *bbOut;
    int       bbIn_idx;
    IRStmt    *stIn;
    Addr32    guestIAddr = 0;
    Int       guestILen  = 0;
    unsigned int loc_instr = 0;

    translations++;

    bbOut           = emptyIRBB();                     // from libvex_ir.h
    bbOut->tyenv    = dopyIRTypeEnv(bbIn->tyenv);      // d:o
    bbOut->next     = dopyIRExpr(bbIn->next);          // d:o
    bbOut->jumpkind = bbIn->jumpkind;

    for( bbIn_idx = 0; bbIn_idx < bbIn->stmts_used; bbIn_idx++ ) {
       stIn = bbIn->stmts[bbIn_idx];
       
       switch( stIn->tag ) {

         case Ist_IMark: 
             guestIAddr = (Addr32) stIn->Ist.IMark.addr;
             guestILen  = stIn->Ist.IMark.len;
             loc_instr++;
             emitIncrementGlobal( bbOut, &instructions, loc_instr );
             loc_instr = 0;
             break;
         case Ist_Exit: 
	     instrumentExit( bbOut, stIn->Ist.Exit.jk, guestIAddr, guestILen, 
                             NULL, loc_instr );
             break;

         case Ist_NoOp:
             break;
         case Ist_AbiHint:
             break;
         case Ist_Put: 
             break;
         case Ist_PutI:
             break;
         case Ist_Tmp:
             // This is an assignment to a temp, so it should be instrumented
             // if the rhs is a Load
             break;
         case Ist_Store:
             // This is a store instruction, so it should be instrumented
             break;
         case Ist_Dirty:
             // This should maybe be instrumented
             break;
         case Ist_MFence:
             break;
         default:
             VG_(tool_panic)("Unknown statement type!");
             break;

       }
       addStmtToIRBB( bbOut, stIn );
    }

    instrumentExit( bbOut, bbIn->jumpkind, guestIAddr, guestILen, bbIn->next, loc_instr );
    return bbOut;
}

static void em_fini(Int exitcode)
{

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
