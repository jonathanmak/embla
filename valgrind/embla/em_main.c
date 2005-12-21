
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
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_options.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_libcassert.h"

#include "libvex_guest_offsets.h"

// smallest prime >= a million
#define  RESULT_ENTRIES   1000003  
#define  FILE_LEN         256
#define  FN_LEN           128

#define  BITS_PER_REF      2
#define  BITS_PER_FRAG    12
#define  BITS_PER_ADDRESS 32
#define  REFS_PER_FRAG    (1 << (BITS_PER_FRAG-BITS_PER_REF))
#define  FRAGS_IN_MAP     (1 << (BITS_PER_ADDRESS-BITS_PER_FRAG))
#define  FRAG_MASK        ((1 << BITS_PER_FRAG) - 1)

#define  GET_FRAG_PTR(m,a)   m[a>>BITS_PER_FRAG]
#define  GET_REF_ADDR(fp,a)  ( &( fp->refs[(a&FRAG_MASK) >> BITS_PER_REF] ) )


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

typedef
   struct {
     Addr32      i_addr;
     StackFrame *context;
   }
   Event;

typedef
   struct {
     Event lastRef;
     Event lastWrite;
   }
   RefInfo;

typedef
   struct {
     RefInfo refs[REFS_PER_FRAG];
   }
   MapFragment;

static MapFragment **map;

static Char buf[512];
static int first_sp = 0;

static int trace_file_fd;

typedef
   struct _RTEntry {
     char *title;
     UInt n_raw, n_war, n_waw;
     struct _RTEntry *next;
   }
   RTEntry;

static RTEntry **result_table;

static UInt hash(Char *s) 
{
   const int hash_const = 257;
   int hash_value = 0;
   while( *s != 0 ) {
      hash_value = (hash_const*hash_value + *s) % RESULT_ENTRIES;
      s++;
   }
   return hash_value;
}

static void getDebugInfo(Addr32 addr, Char file[FILE_LEN], Char fn[FN_LEN], UInt *line)
{
   Bool found_file_line = VG_(get_filename_linenum)(
                              addr,
                              file, FILE_LEN,
                              NULL, 0, NULL,
                              line
                          );
   Bool found_fn = VG_(get_fnname)(addr, fn, FN_LEN);

   if( !found_file_line ) {
      VG_(strcpy)( file, "???" );
      *line = 0;
   }
   if( !found_fn ) {
      VG_(strcpy)( fn, "???" );
   }
}

static RTEntry* getResultEntry(StackFrame *curr_ctx, Addr32 curr_addr, 
                               StackFrame *old_ctx,  Addr32 old_addr)
{
   UInt old_time = old_ctx->seq;
   StackFrame *nca = curr_ctx,
              *tmp = old_ctx;
   Addr32     h_addr=old_addr, 
              t_addr=curr_addr;
   Char       h_file[FILE_LEN], h_fn[FN_LEN];
   Int        h_line;
   Char       t_file[FILE_LEN], t_fn[FN_LEN];
   Int        t_line;
   UInt       hash_value;
   RTEntry    *rp;


   // Find nearest common ancestor of the new ref and the old ref in the call tree
   // t_addr will be the tail address of the dependency, which is either
   //   the address of the instruction making old reference, if the old ref was in nca, or
   //   the address of the call site in the nca
   while( old_time < nca->seq ) {
        t_addr = nca->call_addr;
        nca = nca->parent;
   }

   // Find the head address of the dependency
   while( tmp != nca ) {
        h_addr = tmp->call_addr;
        tmp = tmp->parent;
   }

   // Translate the addresses into line and file numbers

   getDebugInfo( h_addr, h_file, h_fn, &h_line );
   getDebugInfo( t_addr, t_file, t_fn, &t_line );

   // Construct the hash key

   VG_(sprintf)( buf, "%s %s %d %d", h_file, h_fn, t_line, h_line );
   hash_value = hash( buf );

   // Walk the hash chain

   for( rp = result_table[hash_value]; 
        rp != NULL && VG_(strcmp)( buf, rp->title ) != 0;
       rp = rp->next );
   if( rp==NULL ) {
       rp = (RTEntry *) VG_(calloc)( 1, sizeof(RTEntry) );
       rp->next = result_table[hash_value];
       result_table[hash_value] = rp;
       rp->title = (Char *) VG_(calloc)( VG_(strlen)( buf )+1, sizeof(Char) );
       VG_(strcpy)( rp->title, buf );
   }

   return rp;
}
   



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

   map = (MapFragment **) VG_(calloc)(FRAGS_IN_MAP, sizeof(MapFragment*));
   result_table = (RTEntry **) VG_(calloc)(RESULT_ENTRIES, sizeof(RTEntry *));

}

static VG_REGPARM(2)
void recordRead(Addr32 pc, Addr32 addr)
{
    MapFragment *frag = GET_FRAG_PTR(map,addr);
    RefInfo     *refp;
    StackFrame  *nca = current_stack_frame;
    // unsigned int ref_time;
    RTEntry     *res_entry;

    if( frag==NULL ) {
        frag = (MapFragment *) VG_(calloc)(1, sizeof(MapFragment));
        GET_FRAG_PTR(map,addr) = frag;
        if( frag==NULL ) {
            VG_(tool_panic)("Out of memory!");
        }
    }
    
    refp = GET_REF_ADDR(frag,addr);
    refp->lastRef.i_addr = pc;
    refp->lastRef.context = nca;

    
    res_entry = getResultEntry( current_stack_frame,
                                pc, 
                                refp->lastWrite.context, 
                                refp->lastWrite.i_addr );
    

    // to be continued!

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

   if( current->sp+4 > sp ) {
     // this return does not correspond to the appropriate call
     // do nothing
     current_stack_frame=current;
   } else {
     // we have returned and possibly popped a few more words off of the stack
     if( current->ret_addr != target ) {
       // we're not returning to the scene of the crime, sorry, call
       
     }
     // we will not need this retrun address any more
     current_stack_frame=parent;
   }

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
             HChar *hname = "recordRet";
             void  *haddr = &recordRet;
             IRTemp tmp_sp = newIRTemp(bbOut->tyenv, Ity_I32);
             IRTemp tmp_pc = newIRTemp(bbOut->tyenv, Ity_I32);
             IRExpr *exp_sp = IRExpr_Tmp( tmp_sp );
             IRExpr *exp_pc = IRExpr_Tmp( tmp_pc );
             IRExpr *exp_get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );
             IRExpr **args = mkIRExprVec_2( exp_sp, exp_pc );
             IRDirty *dy = unsafeIRDirty_0_N( 2, hname, haddr, args );
             
             if( tgt==NULL ) {
                VG_(tool_panic)("Ret not at end of BB!");
             }

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
