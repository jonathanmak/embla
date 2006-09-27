
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
   - There is an assumption throughout that we run code for a LE 32 bit machine.

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
#define  BUF_SIZE         512

#define  BITS_PER_REF      0
#define  BITS_PER_FRAG    12
#define  BITS_PER_ADDRESS 32
#define  REFS_PER_FRAG    (1 << (BITS_PER_FRAG-BITS_PER_REF))
#define  FRAGS_IN_MAP     (1 << (BITS_PER_ADDRESS-BITS_PER_FRAG))
#define  FRAG_MASK        ((1 << BITS_PER_FRAG) - 1)

#define  GET_FRAG_PTR(m,a)   m[a>>BITS_PER_FRAG]
#define  GET_REF_ADDR(fp,a)  ( &( fp->refs[(a&FRAG_MASK) >> BITS_PER_REF] ) )

#define  BONK(s) VG_(write)( 2, s, VG_(strlen)( s ) )
#define  DPRINT1(s,x)     { VG_(sprintf)( dbuf, s, x );       BONK( dbuf ); }
#define  DPRINT2(s,x,y)   { VG_(sprintf)( dbuf, s, x, y );    BONK( dbuf ); }
#define  DPRINT3(s,x,y,z) { VG_(sprintf)( dbuf, s, x, y, z ); BONK( dbuf ); }

#define  CONT_LEN         1024
#define  FULL_CONTOURS    0

#define N_SMARKS 1000000

static Char h_cont[CONT_LEN], t_cont[CONT_LEN];

#define  SF_HIDDEN        1   // The function owning the frame is HIDDEN
#define  SF_STR_HIDDEN    2   // STRongly HIDDEN; callees inherit the property
#define  SF_SEEN          4   // We have SEEN this stack frame


static struct {
    int elim_stack_alias;
} opt;

typedef unsigned long long int ICount;

typedef unsigned int StatData;

static StatData
   mem_table_frags = 0,
   stack_frames = 0,
   read_list_elements = 0,
   max_read_list_elements = 0;

static unsigned int translations = 0;
static ICount instructions = 0;

static Addr32 shadow_sp, lowest_shadow_sp=0xffffffff, highest_shadow_sp=0;

static Char dbuf[BUF_SIZE] __attribute__((unused));

typedef
   struct _StackFrame {
     Addr32 sp,call_addr,ret_addr;
     unsigned int seq, flags;
     struct _StackFrame *parent;
   }
   StackFrame;

static StackFrame first_stack_frame = {(unsigned int) -1, 0, 0, 0, 0, NULL};
static StackFrame * current_stack_frame = &first_stack_frame;

typedef
   struct {
     Addr32      i_addr;
     ICount      i_count;
     StackFrame *context;
   }
   Event;

typedef 
   struct _EventList {
     Event            ev;
     struct _EventList *next;
   }
   EventList;

// If the last reference was a write, lastRead.ev.context is NULL
typedef
   struct {
     EventList lastRead;
     Event lastWrite;
   }
   RefInfo;

typedef
   struct {
     RefInfo refs[REFS_PER_FRAG];
   }
   MapFragment;

static MapFragment **map;

static Char buf[512+2*CONT_LEN];
static int first_sp = 0;

typedef struct {
   Addr32 sp;
   ICount instr;
} StackMark;

static StackMark smarks[N_SMARKS];
static int topMark=0;

static void bumpCounter(StatData *c, StatData n, StatData *min, StatData *max)
{
   StatData nv = *c + n;
   *c = nv;

   if( n<0 && min!=NULL && nv<*min ) {
      *min = nv;
   } else if( max!=NULL && nv>*max ) {
      *max = nv;
   }
}


const char * trace_file_name = "embla.trace";

typedef
   struct _RTEntry {
     char * h_file;
     char * h_fn;
     char d_inf;
     Int h_line;
     Int t_line;
     char h_inf;
     char t_inf;
     UInt n_raw, n_war, n_waw;
     struct _RTEntry *next;
   }
   RTEntry;

static RTEntry **result_table;

/***********************************************************************
 * Implement the needs_command_line_options for Valgrind.
 **********************************************************************/

static Bool em_process_cmd_line_option(Char* arg)
{
  VG_STR_CLO(arg, "--trace-file", trace_file_name)
  else
    return False;
  
  tl_assert(trace_file_name);
  tl_assert(trace_file_name[0]);
  return True;
}

static void em_print_usage(void)
{  
   VG_(printf)(
"    --trace-file=<name>       store trace data in <name> [embla.trace]\n"
   );
}

static void em_print_debug_usage(void)
{  
}

static UInt hash(const Char *s) 
{
   const int hash_const = 257;
   int hash_value = 0;
   while( *s != 0 ) {
      hash_value = (hash_const*hash_value + *s) % RESULT_ENTRIES;
      s++;
   }
   return hash_value;
}

static VG_REGPARM(2)
void recordSpChange(Addr32 newSp)
{
   int i = topMark;

   // DPRINT2("Calling spchange, new sp=%u, instr=%u\n", (unsigned) newSp, instructions);

   /* smarks[0].sp == (Addr32) -1 acts as sentinel */
   while( newSp >= smarks[i].sp ) { 
      i--;
   }

   i++;

   if( i > N_SMARKS || i < 1 ) {
      VG_(tool_panic)( "Smark overflow" );
   }
   smarks[i].sp = newSp;
   smarks[i].instr = instructions;
   topMark = i;
}

static Bool inStack( Addr32 oldAddr, ICount oldInstr )
{
   unsigned int i;
   
   /*
   DPRINT3("Checking stack, refAddr=%u, old instr=%u, curr instr=%u ...",
          (unsigned) oldAddr, (unsigned) oldInstr, instructions);
   */
   for( i=topMark; smarks[i].instr >= oldInstr; i-- ) {
      if( smarks[i].sp > oldAddr ) {
         /* BONK(" false\n"); */
         return 0;
      }
   }
   /* BONK(" true\n"); */
   return 1;
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

static void checkIfHidden(StackFrame *curr_ctx, Addr32 addr, Bool no_recheck)
{
    Char fname[FN_LEN];

    if( curr_ctx == NULL || ( no_recheck && curr_ctx->flags & SF_SEEN ) ) {
        return;
    }

    if( VG_(get_fnname)(addr, fname, FN_LEN) ) {
        if( ! VG_(strcmp)( fname, "malloc" ) ||
            ! VG_(strcmp)( fname, "calloc" ) ||
            ! VG_(strcmp)( fname, "realloc" ) ) 
        {
            curr_ctx->flags |= SF_HIDDEN | SF_SEEN | SF_STR_HIDDEN;
        } else if ( ! VG_(strncmp)( fname, "_dl", 3 ) ) {
            curr_ctx->flags |= SF_HIDDEN | SF_SEEN;
        } else {
            curr_ctx->flags |= SF_SEEN;
        }
    }
}

static int copyFnName(Char *cont, int idx, Addr32 addr, int hidden) 
{

#if FULL_CONTOURS
   if( CONT_LEN - idx < FN_LEN ) {    // no space left
       return idx;
   }
   if( idx > 0 ) {                     // not the first call; append a ','
       cont[idx++] = ',';
   }
   if( hidden ) {
      cont[idx++] = '#';
   }
   if( ! VG_(get_fnname)( addr, cont+idx, FN_LEN ) ) {
        VG_(strcpy)( cont+idx, "???" );
   }
   return idx + VG_(strlen)( cont+idx );   // start next part *at* the NUL character
#else
   return idx;
#endif

}

static void addNULL(Char *cont, int idx)
{
    cont[idx] = 0;
}

static const Char * makeTitle(const RTEntry * e)
{
  static Char result[BUF_SIZE];
#if FULL_CONTOURS
   VG_(sprintf)( result, "%s %s %s %d%s(%s) %d%s(%s)", 
		 h_file, h_fn, d_inf, h_line, h_inf, h_cont, t_line, t_inf,
		 t_cont );
#else						
   tl_assert(e);
   VG_(sprintf)( result, "%s %s %c %d%c %d%c", 
		 e->h_file, e->h_fn, e->d_inf, e->h_line, e->h_inf, e->t_line,
		 e->t_inf );
#endif
   return result;
}

static Int result_entry_compare(const RTEntry * e1, const RTEntry * e2)
{
  Int result;
  
  tl_assert(e1);
  tl_assert(e2);

  result = VG_(strcmp)(e1->h_file, e2->h_file);
  if (result != 0)
    return result;
  result = VG_(strcmp)(e1->h_fn, e2->h_fn);
  if (result != 0)
    return result;

#define tmp_compare_field(field)		\
  if (e1-> field != e2-> field)			\
    return e1-> field < e2-> field ? -1 : 1

  tmp_compare_field(t_line);
  tmp_compare_field(t_inf);
  tmp_compare_field(h_line);
  tmp_compare_field(h_inf);
  tmp_compare_field(d_inf);
  return 0;

#undef tmp_compare_field
}

static RTEntry* getResultEntry(StackFrame *curr_ctx, Addr32 curr_addr, 
                               StackFrame *old_ctx,  Addr32 old_addr, ICount old_instr,
                               Addr32 ref_addr)
{
   UInt old_time = old_ctx->seq;
   StackFrame *nca,*tmp;
   Addr32     h_addr, t_addr;
   Addr32     h_sp, t_sp;
   Char       h_file[FILE_LEN], h_fn[FN_LEN];
   Char       t_file[FILE_LEN], t_fn[FN_LEN];
   UInt       hash_value;
   RTEntry    *rp;
   Bool       h_hidden, t_hidden;
   int        cont_idx;
   RTEntry    tmp_entry;

   // The tail (source) of the dependence is related to the old reference
   // The head (sink)   of the dependence is related to the new reference

   // Find nearest common ancestor of the new ref and the old ref in the call tree
   // h_addr will be the head address of the dependency, which is either
   //   the address of the instruction making new reference, if the new ref was in nca, or
   //   the address of the call site in the nca

   VG_(memset)(& tmp_entry, 0, sizeof(tmp_entry));
   nca = curr_ctx;
   h_addr = curr_addr;
   h_sp = 0;
   h_hidden = curr_ctx->flags & SF_HIDDEN;

   cont_idx = copyFnName( h_cont, 0, h_addr, h_hidden );
   while( old_time < nca->seq ) {
        h_addr    = nca->call_addr;
        h_sp      = nca->sp;
        h_hidden |= nca->flags & SF_STR_HIDDEN;
        cont_idx = copyFnName( h_cont, cont_idx, nca->call_addr, h_hidden );
        nca = nca->parent;
   }
   addNULL( h_cont, cont_idx );


   // Find the tail address of the dependency
   tmp = old_ctx;
   t_addr = old_addr;
   t_sp = 0;
   t_hidden = old_ctx->flags & SF_HIDDEN;

   cont_idx = copyFnName( t_cont, 0, t_addr, t_hidden );
   while( tmp != nca ) {
        t_addr = tmp->call_addr;
        t_sp   = tmp->sp;
        t_hidden |= tmp->flags & SF_STR_HIDDEN;
        cont_idx = copyFnName( t_cont, cont_idx, tmp->call_addr, t_hidden );
        tmp = tmp->parent;
   }
   addNULL( t_cont, cont_idx );

   // Translate the addresses into line and file numbers

   h_file[0] = '\0';
   h_fn[0] = '\0';
   getDebugInfo( h_addr, h_file, h_fn,
		 & tmp_entry.h_line );

   tmp_entry.h_file = VG_(malloc)(VG_(strlen)(h_file) + 1);
   tl_assert(tmp_entry.h_file);
   VG_(strcpy)(tmp_entry.h_file, h_file);

   tmp_entry.h_fn = VG_(malloc)(VG_(strlen)(h_fn) + 1);
   tl_assert(tmp_entry.h_fn);
   VG_(strcpy)(tmp_entry.h_fn, h_fn);

   getDebugInfo( t_addr, t_file, t_fn,
		 & tmp_entry.t_line );

   // Check if this is a false dependency due to stack aliasing (pop then push)
   // or if it is in the stack or if it is something else

   if( opt.elim_stack_alias && 
       ref_addr >= lowest_shadow_sp && ref_addr <= highest_shadow_sp
       && ! inStack( ref_addr, old_instr ) ) {
       // False aliasing due to stack pop then push 
       tmp_entry.d_inf = 'f';
   } else if( ref_addr >= lowest_shadow_sp && ref_addr <= highest_shadow_sp ) {
       tmp_entry.d_inf = 's';
   } else {
       tmp_entry.d_inf = 'o';
   }

   // Check whether any of the references where made during a call from the 
   // nca activation record or hidden

   tmp_entry.h_inf = curr_ctx==nca ? ' ' : (h_hidden ? 'h' : 'c');
   tmp_entry.t_inf = old_ctx==nca  ? ' ' : (t_hidden ? 'h' : 'c');

   // Construct the hash key

   hash_value = hash( makeTitle(& tmp_entry) );

   // Walk the hash chain

   for( rp = result_table[hash_value]; 
        rp != NULL && result_entry_compare( rp, & tmp_entry ) != 0;
       rp = rp->next )
      ;
   if( rp==NULL ) {
       rp = (RTEntry *) VG_(calloc)( 1, sizeof(RTEntry) );
       if( rp==NULL ) {
           VG_(tool_panic)( "Out of memory" );
       }
       * rp = tmp_entry;
       rp->next = result_table[hash_value];
       result_table[hash_value] = rp;
   }

   return rp;
}
   



static void em_post_clo_init(void)
{
   VG_(clo_vex_control).iropt_level = 0;
   VG_(clo_vex_control).iropt_unroll_thresh = 0;
   VG_(clo_vex_control).guest_chase_thresh = 0;

   VG_(message)(Vg_UserMsg, "Initalising dependency profiling");

   map = (MapFragment **) VG_(calloc)(FRAGS_IN_MAP, sizeof(MapFragment*));
   result_table = (RTEntry **) VG_(calloc)(RESULT_ENTRIES, sizeof(RTEntry *));

   if( map==NULL || result_table==NULL ) {
       VG_(tool_panic)("Out of memory!");
   }

   opt.elim_stack_alias = 1;

   smarks[0].sp = (Addr32) 0xffffffff;
   smarks[0].instr = 0;

}

static RefInfo* getRefInfo(Addr32 addr)
{
    MapFragment *frag = GET_FRAG_PTR(map,addr);
    int          i;

    if( frag==NULL ) {
        frag = (MapFragment *) VG_(calloc)(1, sizeof(MapFragment));
        if( frag==NULL ) {
            VG_(tool_panic)("Out of memory!");
        }
        bumpCounter( &mem_table_frags, 1, NULL, NULL );
        GET_FRAG_PTR(map,addr) = frag;
        for(i=0; i<REFS_PER_FRAG; i++) {
            frag->refs[i].lastWrite.context = &first_stack_frame;
        }
    }

    return GET_REF_ADDR(frag,addr);
}

static void adjust_shadow_sp(Addr32 sp)
{
    shadow_sp = sp;
    if( lowest_shadow_sp > sp ) {
        lowest_shadow_sp = sp;
    }
    if( highest_shadow_sp < sp ) {
        highest_shadow_sp = sp;
    }
}

static VG_REGPARM(3)
void recordLoad(Addr32 pc, Addr32 addr, Addr32 sp)
{
    RefInfo     *refp;
    RTEntry     *res_entry;
    EventList   *ev_list;

    adjust_shadow_sp( sp );
    checkIfHidden( current_stack_frame, pc, 1 );

    refp = getRefInfo( addr );

    res_entry = getResultEntry( current_stack_frame,
                                pc, 
                                refp->lastWrite.context, 
                                refp->lastWrite.i_addr,
                                refp->lastWrite.i_count,
                                addr );
    
    res_entry->n_raw++;

    // DPRINT2("RAW: %s %u\n", res_entry->title, pc);

    if( refp->lastRead.ev.context == NULL ) { // ref before this was write
        refp->lastRead.ev.i_addr = pc;
        refp->lastRead.ev.i_count = instructions;
        refp->lastRead.ev.context = current_stack_frame;
        refp->lastRead.next = NULL;
    } else {
        ev_list = (EventList *) VG_(calloc)( 1, sizeof(EventList) );
        if( ev_list==NULL ) {
            VG_(tool_panic)("Out of memory!");
        }
        bumpCounter( &read_list_elements, 1, NULL, &max_read_list_elements );   
        ev_list->ev.i_addr = pc;
        ev_list->ev.i_count = instructions;
        ev_list->ev.context = current_stack_frame;
        ev_list->next = refp->lastRead.next;
        refp->lastRead.next = ev_list;
    }

}

static VG_REGPARM(3)
void recordStore(Addr32 pc, Addr32 addr, Addr32 sp)
{
    RefInfo     *refp=getRefInfo( addr );
    RTEntry     *res_entry;
    EventList   *ev_list, *ev_next;

    adjust_shadow_sp( sp );
    checkIfHidden( current_stack_frame, pc, 1 );

    // is it a WAR or a WAW?
    if( refp->lastRead.ev.context == NULL ) {        // DONE !
        // last reference was a write: a WAW
        res_entry = getResultEntry( current_stack_frame,
                                    pc, 
                                    refp->lastWrite.context, 
                                    refp->lastWrite.i_addr,
                                    refp->lastWrite.i_count,
                                    addr );
        res_entry->n_waw++;

        // DPRINT2("WAW: %s %u\n", res_entry->title, pc);
    } else {
        // last reference was a read: a WAR
        for( ev_list = &(refp->lastRead); ev_list != NULL; ev_list = ev_list->next ) {
            res_entry = getResultEntry( current_stack_frame,
                                        pc, 
                                        ev_list->ev.context, 
                                        ev_list->ev.i_addr,
                                        ev_list->ev.i_count,
                                        addr );
            res_entry->n_war++;
        }
        // DPRINT2("WAR: %s %u\n", res_entry->title, pc);
    }

    // last reference is now a store
    refp->lastRead.ev.context = NULL;
    for( ev_list = refp->lastRead.next; ev_list != NULL; ev_list = ev_next ) {
        ev_next = ev_list->next;
        VG_(free)( ev_list );
        bumpCounter( &read_list_elements, -1, NULL, NULL );
    }
    refp->lastRead.next = NULL;

    refp->lastWrite.i_addr = pc;
    refp->lastWrite.i_count = instructions;
    refp->lastWrite.context = current_stack_frame;

}

static VG_REGPARM(3)
void recordCall(int sp, int ca, int ra) 
{
    StackFrame * newFrame = (StackFrame *) VG_(calloc)(1,sizeof(StackFrame));

    if( newFrame==NULL ) {
        VG_(tool_panic)("Out of memory!");
    }
    bumpCounter( &stack_frames, 1, NULL, NULL );

    checkIfHidden( current_stack_frame, ca, 0 );
    if( first_sp==0 ) {
       first_sp = sp;
    }

    newFrame->parent    = current_stack_frame;
    newFrame->sp        = sp;
    newFrame->call_addr = ca;
    newFrame->ret_addr  = ra;
    newFrame->seq       = instructions;
    newFrame->flags     = 0;

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

   if( first_sp==0 ) {
      first_sp = sp;
   }

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

   if( current_stack_frame == NULL ) {
     current_stack_frame = &first_stack_frame;
   }

}


static void instrumentLoad( IRBB *bbOut, Addr32 pc, IRExpr *exp_addr )
{
    HChar *hname = "recordLoad";
    void  *haddr = &recordLoad;

    IRTemp tmp_sp = newIRTemp( bbOut->tyenv, Ity_I32 );
    IRExpr *exp_sp = IRExpr_Tmp( tmp_sp );
    IRExpr *exp_get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );

    IRExpr *exp_pc = IRExpr_Const( IRConst_U32( pc ) );
    IRExpr **args = mkIRExprVec_3( exp_pc, exp_addr, exp_sp );
    IRDirty *dy = unsafeIRDirty_0_N( 3, hname, haddr, args );

    addStmtToIRBB( bbOut, IRStmt_Tmp( tmp_sp, exp_get_sp ) );
    addStmtToIRBB( bbOut, IRStmt_Dirty(dy) );
}

static void instrumentStore( IRBB *bbOut, Addr32 pc, IRExpr *exp_addr )
{
    HChar *hname = "recordStore";
    void  *haddr = &recordStore;

    IRTemp tmp_sp = newIRTemp( bbOut->tyenv, Ity_I32 );
    IRExpr *exp_sp = IRExpr_Tmp( tmp_sp );
    IRExpr *exp_get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );

    IRExpr *exp_pc = IRExpr_Const( IRConst_U32( pc ) );
    IRExpr **args = mkIRExprVec_3( exp_pc, exp_addr, exp_sp );
    IRDirty *dy = unsafeIRDirty_0_N( 3, hname, haddr, args );

    addStmtToIRBB( bbOut, IRStmt_Tmp( tmp_sp, exp_get_sp ) );
    addStmtToIRBB( bbOut, IRStmt_Dirty(dy) );
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

static void emitSpChange(IRBB *bbOut)
{
    HChar *hname = "recordSpChange";
    void  *haddr = &recordSpChange;

    IRTemp tmp_sp = newIRTemp(bbOut->tyenv, Ity_I32);
    IRExpr *exp_sp = IRExpr_Tmp( tmp_sp );
    IRExpr *exp_get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );
    IRExpr **args = mkIRExprVec_1( exp_sp );
    IRDirty *dy = unsafeIRDirty_0_N( 1, hname, haddr, args );
    
    addStmtToIRBB( bbOut, IRStmt_Tmp( tmp_sp, exp_get_sp ) );
    addStmtToIRBB( bbOut, IRStmt_Dirty(dy) );
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
			   Addr64 orig_addr_noredir, VexGuestExtents * vge,
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
             emitSpChange( bbOut );

             guestIAddr = (Addr32) stIn->Ist.IMark.addr;
             guestILen  = stIn->Ist.IMark.len;
             loc_instr++;
             emitIncrementGlobal( bbOut, &instructions, loc_instr );
             loc_instr = 0;
             break;
         case Ist_Exit: 
	     instrumentExit( bbOut, stIn->Ist.Exit.jk, guestIAddr, guestILen, 
                             IRExpr_Const( stIn->Ist.Exit.dst ), loc_instr );
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
             if( stIn->Ist.Tmp.data->tag == Iex_Load ) {
                 instrumentLoad( bbOut, guestIAddr, stIn->Ist.Tmp.data->Iex.Load.addr );
             }
             break;
         case Ist_Store:
             // This is a store instruction, so it should be instrumented
             instrumentStore( bbOut, guestIAddr, stIn->Ist.Store.addr );
             break;
         case Ist_Dirty:
             // This should maybe be instrumented, but we'll leave it for later
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

static void printResultTable(const Char * traceFileName)
{
   int      i;
   RTEntry *entry;
   SizeT results_buf_size = 100;
   RTEntry * result_array = VG_(malloc)(sizeof(RTEntry) * results_buf_size);
   SizeT num_results = 0;
   int fd = -1;

   tl_assert(result_array);
   for( i=0; i<RESULT_ENTRIES; i++ )
     for( entry=result_table[i]; entry != NULL; entry=entry->next )
       if ((VG_(strcmp)(entry->h_file, "???") != 0) &&
	   (VG_(strcmp)(entry->h_fn, "???") != 0))
       {
	 ++num_results;
	 if (num_results >= results_buf_size)
	 {
	   results_buf_size *= 2;
	   result_array = VG_(realloc)(result_array,
				       sizeof(RTEntry) * results_buf_size);
	   tl_assert(result_array);
	 }
	 result_array[num_results - 1] = * entry;
       }

   VG_(ssort)((void *) result_array, num_results, sizeof(RTEntry),
	      (Int (*)(void *, void *)) result_entry_compare);
   
   if (VG_(strcmp)(traceFileName, "-") != 0)
   {
      SysRes sres;
      sres = VG_(open)((Char *) traceFileName, 
		       VKI_O_CREAT | VKI_O_TRUNC | VKI_O_WRONLY,
		       VKI_S_IRUSR | VKI_S_IWUSR | VKI_S_IRGRP);
     if( sres.isError ) {
       VG_(message)(Vg_UserMsg, "Trace file could not be opened");
       return;
     }
     fd = sres.val;
   }
   for (i = 0; i < num_results; ++i)
   {
     VG_(sprintf)( buf, "%s %d %d %d\n", 
		   makeTitle(& result_array[i]), result_array[i].n_raw,
		   result_array[i].n_war, result_array[i].n_waw );
     if (VG_(strcmp)(traceFileName, "-") != 0)
       VG_(write)( fd, buf, VG_(strlen)( buf ) );
     else
       VG_(printf)("%s", buf);
   }
   if (VG_(strcmp)(traceFileName, "-") != 0)
     VG_(close)( fd );
}

static void em_fini(Int exitcode)
{
   VG_(message)(Vg_UserMsg, "Dependency trace has finished, storing in %s",
		trace_file_name);
   printResultTable( trace_file_name );
   VG_(message)(Vg_UserMsg, "Max mem frags:    %10u (%10u bytes)", 
                            mem_table_frags, 
                            mem_table_frags*sizeof( MapFragment ) );
   VG_(message)(Vg_UserMsg, "Max stack frames: %10u (%10u bytes)", 
                            stack_frames,
                            stack_frames*sizeof( StackFrame ) );
   VG_(message)(Vg_UserMsg, "Max read list:    %10u (%10u bytes)", 
                            max_read_list_elements,
                            max_read_list_elements*sizeof( EventList ) );
   VG_(message)(Vg_UserMsg, "Instructions:    %11llu", instructions);
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
   VG_(needs_command_line_options)(em_process_cmd_line_option,
                                   em_print_usage,
                                   em_print_debug_usage);

   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(em_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
