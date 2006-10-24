
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
   - Funny things will happen if we access the same place with words and bytes
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

#define IFDID( c ) {if( did_gc>2 ) { c } }
static unsigned did_gc=0;

#define  CONT_LEN         1024
#define  FULL_CONTOURS    0

#define N_SMARKS          1000000
#define N_STACK_FRAMES    1000000
#define N_TRACE_RECS     40000000

static Char h_cont[CONT_LEN], t_cont[CONT_LEN];

#define  SF_HIDDEN        1   // The function owning the frame is HIDDEN
#define  SF_STR_HIDDEN    2   // STRongly HIDDEN; callees inherit the property
#define  SF_SEEN          4   // We have SEEN this stack frame

//
// Tags with i_addr != 0
//
#define  TPT_REG                  0    // A regular node
#define  TPT_OPEN                 1    // An open call header; corresponds to a frame
#define  TPT_CLOSED               2    // A closed call header
#define  TPT_REG_OR_CLOSED_LIVE   3    // Valid from phase 1-3 in compaction

//
// Tags with i_addr == 0
//
#define  TPT_RET          0   // A return header
#define  TPT_BRIDGE       1   // Bridging a gap of dead nodes
#define  TPT_RET_LIVE     2   // A live return header (important for phase 2)

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
static unsigned millions;

static Addr32 shadow_sp, lowest_shadow_sp=0xffffffff, highest_shadow_sp=0;

static Char dbuf[BUF_SIZE] __attribute__((unused));

#ifdef TAGGED_PTR_BITFIELD

typedef 
   struct {
     unsigned addr:30;
     unsigned flag1:1;
     unsigned flag2:1;
   }
   TaggedPtr;

static TaggedPtr mkTaggedPtr(void *ptr, unsigned flag1, unsigned flag2)
{
   TaggedPtr t;
   if( ( (unsigned) ptr & 3 ) != 0 ) {
      VG_(tool_panic)( "Unaligned pointer in mkTaggedPtr" );
   }
   t.addr = ((unsigned) ptr) >> 2; 
   t.flag1 = flag1;
   t.flag2 = flag2;

   return t;
}

static TaggedPtr mkTaggedPtr2(void *ptr, unsigned flag)
{
   return mkTaggedPtr( ptr, (flag>>1)&1, flag&1 );
}

#define TPTR_UNTAG( t_ptr ) ( t_ptr.addr << 2 )
#define ToTrP( t_ptr ) ( (TraceRec   *) TPTR_UNTAG( t_ptr ) )
#define ToStP( t_ptr ) ( (StackFrame *) TPTR_UNTAG( t_ptr ) )

#define TP_GET_FLAGS( t_ptr ) ( t_ptr.flag1<<1 | t_ptr.flag2 )
#define TP_GET_FLAG1( t_ptr ) ( t_ptr.flag1 )
#define TP_GET_FLAG2( t_ptr ) ( t_ptr.flag2 )

#define EQUAL_EVENT(x,y) (TP_GET_FLAGS( x ) == TP_GET_FLAGS( y ) && x.addr == y.addr )

#else

typedef unsigned TaggedPtr;

static TaggedPtr mkTaggedPtr(void *ptr, unsigned flag1, unsigned flag2)
{
   if( ( (unsigned) ptr & 3 ) != 0 ) {
      VG_(tool_panic)( "Unaligned pointer in mkTaggedPtr" );
   }

   return ( (unsigned) ptr ) | ( ( flag1<<1 ) & 2 ) | ( flag2 & 1 );
}

static TaggedPtr mkTaggedPtr2(void *ptr, unsigned flag)
{
   if( ( (unsigned) ptr & 3 ) != 0 ) {
      VG_(tool_panic)( "Unaligned pointer in mkTaggedPtr2" );
   }

   return ( (unsigned) ptr ) | ( flag & 3 );
}

#define TPTR_UNTAG( t_ptr ) ( t_ptr & ~3 )
#define ToTrP( t_ptr ) ( (TraceRec   *) TPTR_UNTAG( t_ptr ) )
#define ToStP( t_ptr ) ( (StackFrame *) TPTR_UNTAG( t_ptr ) )

#define TP_GET_FLAGS( t_ptr ) ( t_ptr & 3 )
#define TP_GET_FLAG1( t_ptr ) ( ( t_ptr >> 1 ) & 1 )
#define TP_GET_FLAG2( t_ptr ) ( t_ptr & 1 )

#define EQUAL_EVENT(x,y) ( x == y )

#endif

typedef 
   struct {
      Addr32    i_addr;
      TaggedPtr link;
   }
   TraceRec;

static TraceRec *trace_pile;
static TraceRec *last_trace_rec;

static unsigned long int n_calls_to_newTR= 0; 

static TraceRec * newTraceRec(Addr32 i_addr, TaggedPtr link)
{
   n_calls_to_newTR++;
   if( last_trace_rec >= trace_pile + N_TRACE_RECS ) {
       DPRINT1( "Bailing out after %u calls\n", n_calls_to_newTR );
       VG_(tool_panic)( "Trace pile overflow" );
   }
   last_trace_rec++;
   last_trace_rec->i_addr = i_addr;
   last_trace_rec->link   = link;
   return last_trace_rec;
}

typedef struct {
   Addr32    sp;
   TraceRec *tr;
} StackMark;

static StackMark smarks[N_SMARKS];
static int topMark=0;


typedef
   struct _StackFrame {
     Addr32 sp,call_addr,ret_addr;
     unsigned int flags;
     // struct _StackFrame *parent;
     TraceRec *call_header;
     StackMark *stack_mark;
   }
   StackFrame;

static StackFrame stack_base[N_STACK_FRAMES] =
       {
         {(unsigned int) -1, 0, 0, 0, NULL, smarks} // initializing the first
       };
static StackFrame * current_stack_frame = stack_base;

/* Relation between the trace pile and the stack:
   - stack_frame[i] corresponds to a region in the trace pile
     - stack_frame[i].trace_region points to the oldest trace_rec
     - stack_frame[i+1].trace_region points one trace rec beyond the region
       except for the top stack frame (current_stack_frame == stack_frame+i)
       where last_trace_rec plays that role.
   - we have ToStP( stack_frame[i].trace_region.link ) == stack_frame+i

*/

static TaggedPtr newRegularEvent(StackFrame *frame, Addr32 i_addr)
{
   TraceRec  *tr   = newTraceRec( i_addr, mkTaggedPtr2( frame->call_header, TPT_REG ) );
   unsigned  hflag = ( frame->flags & SF_HIDDEN ) != 0 ? 1 : 0;

   return mkTaggedPtr( tr, hflag, 0 );
}

typedef TaggedPtr Event;

typedef 
   struct _EventList {
     Event            ev;
     struct _EventList *next;
   }
   EventList;

EventList *freeEvent = NULL;

static EventList *consEvent(Event e, EventList *t)
{
   int N = 1024, i;
   EventList *l;

   if( freeEvent==NULL ) {
      freeEvent = (EventList *) VG_(calloc)( N, sizeof( EventList ) );
      if( freeEvent == NULL ) {
         VG_(tool_panic)( "Out of memory for consEvent" );
      }
      for( i=0; i<N-1; i++ ) {
        freeEvent[i].next = freeEvent+i+1;
      }
      freeEvent[N-1].next = NULL;
   }
   l = freeEvent;
   freeEvent = freeEvent->next;
   l->ev   = e;
   l->next = t;
   return l;
}

static void deleteEvent(EventList *l)
{
   l->next = freeEvent;
   freeEvent = l;
}
   

// If the last reference was a write, lastRead is NULL
typedef
   struct {
     EventList *lastRead;
     Event      lastWrite;
   }
   RefInfo;

typedef
   struct {
     RefInfo refs[REFS_PER_FRAG];
   }
   MapFragment;

static MapFragment **map;

static Char buf[512+2*CONT_LEN];

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

static RTEntry mock_rtentry = {"mock", "", 'o', 0, 0, 'c', 'c', 0, 0, 0, NULL};

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

   /* smarks[0].sp == (Addr32) -1 acts as sentinel */
   while( newSp >= smarks[i].sp ) { 
      i--;
   }

   i++;

   if( i > N_SMARKS || i < 1 ) {
      VG_(tool_panic)( i > N_SMARKS ? "Smark overflow" : "Smark underflow" );
   }
   smarks[i].sp = newSp;
   smarks[i].tr = last_trace_rec;
   topMark = i;

   // if( instructions / 1000000 > millions ) {
   //    BONK( "-\n" );
   // }
}

static Bool inStack( Addr32 oldAddr, TraceRec *oldTR )
{
   unsigned int i;
   
   for( i=topMark; smarks[i].tr >= oldTR; i-- ) {
      if( smarks[i].sp > oldAddr ) {
         return 0;
      }
   }
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

static int howHidden( Addr32 addr )
{
    Char fname[FN_LEN];

    if( VG_(get_fnname)(addr, fname, FN_LEN) ) {
        if( ! VG_(strcmp)( fname, "malloc" ) ||
            ! VG_(strcmp)( fname, "calloc" ) ||
            ! VG_(strcmp)( fname, "realloc" ) ) 
        {
            return SF_SEEN | SF_HIDDEN | SF_STR_HIDDEN;
        } else if ( ! VG_(strncmp)( fname, "_dl", 3 ) ) {
            return SF_SEEN | SF_HIDDEN;
        } else {
            return SF_SEEN;
        }
    } else {
        return 0;
    }
}

static void checkIfHidden(StackFrame *frame, Addr32 addr, Bool no_recheck)
{
    if( frame == NULL || ( no_recheck && frame->flags & SF_SEEN ) ) {
        return;
    } else {
        frame->flags |= howHidden( addr );
        if( frame-1 > stack_base && frame[-1].flags&SF_STR_HIDDEN ) {
           // propagate from caller if caller is strongly hidden
           frame->flags |= SF_SEEN | SF_HIDDEN | SF_STR_HIDDEN;
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

static void check(int c, Char *s)
{
   if( !c ) {
     VG_(tool_panic)( s );
   }
}

static Event forward( Event e, int delta )
{
   // 'e' is part of memory table

   TraceRec *tp = ToTrP( e ); // tp now points at a trace rec in the pile
   unsigned tp_flags = TP_GET_FLAGS( tp->link );

   while( tp_flags == TPT_REG || tp_flags == TPT_CLOSED ) {
      tp = ToTrP( tp->link );
      tp_flags = TP_GET_FLAGS( tp->link );
   }
   return mkTaggedPtr2( ToTrP( tp->link ) - delta, TP_GET_FLAGS( e ) );
}

#define N_READ_TABLE_BITS 10
#define N_READ_TABLE_ENTRIES (1<<N_READ_TABLE_BITS)
#define READ_TABLE_INDEX(i) ((unsigned) i & (N_READ_TABLE_ENTRIES-1))

static EventList *readtable[N_READ_TABLE_ENTRIES];
static int nonempty[N_READ_TABLE_ENTRIES], modlist;

static void init_read_table(void)
{
   int i;
   for( i=0; i<N_READ_TABLE_ENTRIES; i++ ) {
     readtable[i] = NULL;
     nonempty[i] = -1;
   }
}

#if NAIVE_COMPACT_READS

static EventList * compactReads( EventList *in_p, int delta )
{
   EventList *lp,*new_p=NULL,*next_p;

   for( lp=in_p; lp != NULL; lp=next_p ) {
      Event ev = forward( lp->ev, delta );
      EventList *p;
      int found = 0;
      next_p = lp->next;
      for( p=new_p; p!=NULL; p=p->next ) {
         if( EQUAL_EVENT( p->ev, ev ) ) {
            found = 1;
         }
      }
      if( found ) {
         deleteEvent( lp );
      } else {
         lp->next = new_p;
         lp->ev = ev;
         new_p = lp;
      }
   }
   return new_p;
}

#else

#define ToUnsigned( e ) ( e )

static EventList * compactReads( EventList *in_p, int delta )
{
   EventList *lp,*new_p=NULL,*next_p=NULL;
   int i;

   modlist = -1;

   for( lp=in_p; lp != NULL; lp=next_p ) {
      Event ev = forward( lp->ev, delta );
      EventList *p;
      int found = 0;
      unsigned ev_key = READ_TABLE_INDEX( ToUnsigned( ev ) );
      next_p = lp->next;
      for( p=readtable[ev_key]; p!=NULL; p=p->next ) {
         if( EQUAL_EVENT( p->ev, ev ) ) {
            found = 1;
         }
      }
      if( found ) {
         deleteEvent( lp );
      } else {
         if( readtable[ev_key] == NULL ) {
            nonempty[ev_key] = modlist;
            modlist = ev_key;
         }
         lp->next = readtable[ev_key];
         lp->ev = ev;
         readtable[ev_key] = lp;
      }
   }
   new_p = NULL;
   for( i=modlist; i != -1; i=nonempty[i] ) {
      for( lp=readtable[i]; lp != NULL; lp = lp->next ) {
         next_p = lp;
      }
      next_p->next = new_p;
      new_p = readtable[i];
      readtable[i] = NULL;
   }
   return new_p;
}

#endif

static void compact(void)
{

   TraceRec   *tp,*ap,*last_open,*last_closed;
   StackFrame *sp;
   StackMark  *mp;
   int         delta, i, j;
   RefInfo    *info;

   // Phase 1: Determine live trace recs and find new locations. Backwards
   // Phase 2: Update all pointers into the trace pile
   //             - Memory table
   //             - Stack marks
   //             - Shadow stack
   // Phase 3: Construct list for forwards traversal. Backwards
   // Phase 4: Relocate live trace recs. Forwards

   // Phase 1:
   tp = last_trace_rec;
   ap = last_trace_rec;
   sp = current_stack_frame;
   mp = smarks+topMark;
   while( tp >= trace_pile ) {
      if( tp->i_addr != 0 ) {
        switch( TP_GET_FLAGS( tp->link ) ) {
          // Regular
          case TPT_REG:
            tp->link = mkTaggedPtr2( ap, TPT_REG_OR_CLOSED_LIVE );
            ap--;
            break;

          // Open header
          case TPT_OPEN:
            tp->link = mkTaggedPtr2( ap, TPT_OPEN );
            ap--;
            break;

          // Closed header
          case TPT_CLOSED:
            tp->link = mkTaggedPtr2( ap, TPT_REG_OR_CLOSED_LIVE );
            ap--;
            break;

          // Unused
          case TPT_REG_OR_CLOSED_LIVE:
            VG_(tool_panic)( "Unexpected tag in phase 1 of compaction" );
            break;
        }
      } else {
	// Return
        tp->link = mkTaggedPtr2( ToTrP( tp->link ), TPT_RET_LIVE );
        tp = ToTrP( tp->link );
        check( TP_GET_FLAGS( tp->link ) == TPT_CLOSED,
               "Return header not pointing at closed call in phase 1" );
        tp->link = mkTaggedPtr2( ap-1, TPT_REG_OR_CLOSED_LIVE );
        ap-=2; // Return is also saved
      }
      tp--;
   }
   delta = ap + 1 - trace_pile;

   IFDID( BONK( "Phase 1 done\nPhase 2 starting\n" ); )
   
   // Phase 2:
   for( sp=stack_base; sp <= current_stack_frame; sp++ ) {
     check( TP_GET_FLAGS( sp->call_header->link ) == TPT_OPEN, 
            "Wrong header pointed to by sp->call_header" );
     sp->call_header = ToTrP( sp->call_header->link ) - delta;
   }
   IFDID( BONK( "   stack\n" ); )
   for( mp=smarks; mp<=smarks+topMark; mp++ ) {
     int flag = TP_GET_FLAGS( mp->tr->link );
     unsigned addr = mp->tr->i_addr;
     if( flag!=TPT_OPEN && flag!=TPT_REG_OR_CLOSED_LIVE && (flag!=TPT_RET_LIVE || addr) ) 
     { 
        mp->tr = mp==smarks ? trace_pile : (mp-1)->tr;
     } else if( flag==TPT_RET_LIVE && addr==0 ) {
        TraceRec * header = ToTrP( mp->tr->link );
        mp->tr = ToTrP( header->link ) - delta + 1;
     } else {
        mp->tr = ToTrP( mp->tr->link ) - delta;
     }
   }
   IFDID( BONK( "   stack marks\n" ); )
   for( i=0; i<FRAGS_IN_MAP; i++ ) {
     if( map[i] != NULL ) {
       for( j=0; j<REFS_PER_FRAG; j++ ) {
         info = map[i]->refs + j;
         info->lastWrite = forward( info->lastWrite, delta );
         info->lastRead = compactReads( info->lastRead, delta );
       }
     }
   }

   IFDID( BONK( "Phase 2\n" ); )
   
   // Phase 3:
   tp = last_trace_rec;
   while( tp >= trace_pile ) {
      if( tp->i_addr != 0 ) {
        switch( TP_GET_FLAGS( tp->link ) ) {
          // Dead trace recs
          case TPT_REG:
          case TPT_CLOSED:
            tp->link = mkTaggedPtr2( tp+1, TPT_BRIDGE );
            tp->i_addr = 0;
            break;

          // Open header
          case TPT_OPEN:
            // Nothing needs to be done
            break;

          // Live regular or closed header
          case TPT_REG_OR_CLOSED_LIVE:
            // Certainly a regular since it was not pointed at by return
            tp->link = mkTaggedPtr2( NULL, TPT_REG );
            break;
        }
      } else {
	// Return
        TraceRec *h_ptr = ToTrP( tp->link );
        check( TP_GET_FLAGS( tp->link ) == TPT_RET_LIVE, 
               "Return header with funny tag in phase 3" );
        if( TP_GET_FLAGS( h_ptr->link ) != TPT_REG_OR_CLOSED_LIVE ) {
          VG_(tool_panic)( "Return header target with funny tag in phase 3" );
        }
        check( h_ptr < tp, "TPT_RET_LIVE pointing up in phase 3" );
        if( h_ptr+1 != tp ) {
          h_ptr[1].link = mkTaggedPtr2( tp, TPT_BRIDGE );
          h_ptr[1].i_addr = 0;
        }
        h_ptr->link = mkTaggedPtr2( NULL, TPT_CLOSED );
        tp = h_ptr;
      }
      tp--;
   }

   IFDID( BONK( "Phase 3\n" ); )
   
   // Phase 4:
   tp = trace_pile;
   ap = trace_pile;
   last_open = NULL;
   last_closed=NULL;
   sp = stack_base;
   while( tp <= last_trace_rec ) {
      if( tp->i_addr != 0 ) {
        switch( TP_GET_FLAGS( tp->link ) ) {
          // Regular trace rec
          case TPT_REG:
            ap->link = mkTaggedPtr2( last_open, TPT_REG );
            ap->i_addr = tp->i_addr;
            ap++;
            break;

          // Closed call header
          case TPT_CLOSED:
            check( last_closed == NULL, "Two TPT_CLOSED with no TPT_RET" );
            last_closed = ap;
            ap->link = mkTaggedPtr2( last_open, TPT_CLOSED );
            ap->i_addr = tp->i_addr;
            ap++;
            break;

          // Open header
          case TPT_OPEN:
            check( last_closed == NULL, "TPT_OPEN between TPT_CLOSED and TPT_RET" );
            last_open = ap;
            ap->link = mkTaggedPtr2( sp->call_header, TPT_OPEN );
            ap->i_addr = tp->i_addr;
            ap++;
            sp++;
            break;

          // Live regular or closed header
          case TPT_REG_OR_CLOSED_LIVE:
            VG_(tool_panic)( "TPT_REG_OR_CLOSED_LIVE in phase 4" );
            break;
        }
        tp++;
      } else {
	// Return or bridge
        switch( TP_GET_FLAGS( tp->link ) ) {
          case TPT_RET_LIVE:
            check( last_closed != NULL, "last_closed == NULL at TPT_RET_LIVE" );
            ap->link = mkTaggedPtr2( last_closed, TPT_RET );
            ap->i_addr = 0;
            last_closed = NULL;
            ap++;
            tp++;
            break;

          case TPT_BRIDGE:
            tp = ToTrP( tp->link );
            break;

          case TPT_RET:
            VG_(tool_panic)( "TPT_RET in phase 4" );
            break;
        }
      }
   }
   last_trace_rec = ap-1;
   IFDID( BONK( "Phase 4\n" ); )
   
}

#define N_LEAST 1000000
static unsigned n_live_last = N_LEAST, 
                max_use = N_TRACE_RECS - 100000;

static void gc(void) 
{
   unsigned n_used = last_trace_rec - trace_pile;

   if( n_used > max_use || n_used > 10 * n_live_last ) {
      did_gc++;
      DPRINT2("[ %llu  %u\n", instructions, n_used);
      compact( );
      n_live_last = last_trace_rec - trace_pile;
      DPRINT1("%u ]\n", n_live_last);
      if( n_live_last < N_LEAST ) {
         n_live_last = N_LEAST;
      }
   }
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
                               Event old_event,
                               Addr32 ref_addr)
{
   TraceRec *old_tr = ToTrP( old_event ),
            *nca_tr = ToTrP( old_tr->link );
   Addr32   h_addr, t_addr;
   Char     h_file[FILE_LEN], h_fn[FN_LEN];
   Char     t_file[FILE_LEN], t_fn[FN_LEN];
   Int      h_ind, t_ind;
   UInt     hash_value;
   RTEntry  *rp;
   Bool     h_hidden, t_hidden;
   RTEntry    tmp_entry;

   // IFDID( BONK("getResultEntry\n   looks for NCA\n"); );

   // The tail (source) of the dependence is related to the old reference
   // The head (sink)   of the dependence is related to the new reference

   // Find nearest common ancestor of the new ref and the old ref in the call tree
   // h_addr will be the head address of the dependency, which is either
   //   the address of the instruction making new reference, if the new ref was 
   //     in nca, or
   //   the address of the call site in the nca

   VG_(memset)(& tmp_entry, 0, sizeof(tmp_entry));

   while( TP_GET_FLAGS( nca_tr->link ) != TPT_OPEN ) { // tag 1 is open header
       // IFDID( DPRINT3( "      nca_tr=%u, nca tag=%u, nca i_addr=%u\n", 
       //                 nca_tr-trace_pile, 
       //                 TP_GET_FLAGS( nca_tr->link ), nca_tr->i_addr ); );
       old_tr = nca_tr;
       nca_tr = ToTrP( nca_tr->link );
   }
   // nca_tr now points to call header for NCA

   // IFDID( DPRINT1("   found NCA, TR %u\n", nca_tr - trace_pile ); );

   t_ind = TP_GET_FLAGS( old_tr->link ) == TPT_CLOSED; // tag 2 is closed header
   h_ind = nca_tr != curr_ctx->call_header;

   // IFDID( BONK("   found t_ind, h_ind\n" ); );

   t_addr = old_tr->i_addr;
   h_addr = h_ind ? ToStP( nca_tr->link )[1].call_header->i_addr : curr_addr;

   // IFDID( BONK("   found t_addr, h_addr\n" ); );

   t_hidden = TP_GET_FLAG1( old_event );
   h_hidden = curr_ctx->flags & SF_HIDDEN;

   // IFDID( BONK("   found t_hidden, h_hidden\n" ); );

   // Now Lalle's code begins in earnest!

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

   // Here is a mixture

   // Check if this is a false dependency due to stack aliasing (pop then push)
   // or if it is in the stack or if it is something else

   if( opt.elim_stack_alias && 
       ref_addr >= lowest_shadow_sp && ref_addr <= highest_shadow_sp
       && ! inStack( ref_addr, old_tr ) ) {
       // False aliasing due to stack pop then push 
       tmp_entry.d_inf = 'f';
   } else if( ref_addr >= lowest_shadow_sp && ref_addr <= highest_shadow_sp ) {
       tmp_entry.d_inf = 's';
   } else {
       tmp_entry.d_inf = 'o';
   }

   // Check whether any of the references where made during a call from the 
   // nca activation record or hidden

   tmp_entry.h_inf = !h_ind ? ' ' : (h_hidden ? 'h' : 'c');
   tmp_entry.t_inf = !t_ind ? ' ' : (t_hidden ? 'h' : 'c');


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

   // IFDID( BONK("... finished\n"); );

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
   trace_pile = (TraceRec *) VG_(calloc)( N_TRACE_RECS, sizeof( TraceRec ) );

   if( map==NULL || result_table==NULL || trace_pile==NULL ) {
       VG_(tool_panic)("Out of memory!");
   }

   opt.elim_stack_alias = 1;

   smarks[0].sp = (Addr32) 0xffffffff;
   smarks[0].tr = trace_pile+1;

   trace_pile[0].i_addr = 1;
   trace_pile[0].link = mkTaggedPtr2(stack_base, TPT_OPEN);

   trace_pile[1].i_addr = 1;
   trace_pile[1].link = mkTaggedPtr2(trace_pile, TPT_REG);

   last_trace_rec = trace_pile+1;
   stack_base->call_header = trace_pile;

   init_read_table( );

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
            frag->refs[i].lastWrite = mkTaggedPtr2( trace_pile+1, TPT_REG );
            frag->refs[i].lastRead = NULL;
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

    adjust_shadow_sp( sp );
    checkIfHidden( current_stack_frame, pc, 1 );

    refp = getRefInfo( addr );

    res_entry = getResultEntry( current_stack_frame,
                                pc, 
                                refp->lastWrite, 
                                addr );
    
    res_entry->n_raw++;

    // DPRINT2("RAW: %s %u\n", res_entry->title, pc);

    refp->lastRead = consEvent( newRegularEvent( current_stack_frame, pc ), 
                                refp->lastRead );

    gc( );
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
    if( refp->lastRead == NULL ) {        // DONE !
        // last reference was a write: a WAW
        res_entry = getResultEntry( current_stack_frame,
                                    pc, 
                                    refp->lastWrite, 
                                    addr );
        res_entry->n_waw++;

        // DPRINT2("WAW: %s %u\n", res_entry->title, pc);
    } else {
        // last reference was a read: a WAR
        for( ev_list = refp->lastRead; ev_list != NULL; ev_list = ev_list->next ) {
            res_entry = getResultEntry( current_stack_frame,
                                        pc, 
                                        ev_list->ev, 
                                        addr );
            res_entry->n_war++;
        }
        // DPRINT2("WAR: %s %u\n", res_entry->title, pc);
    }

    // last reference is now a write
    // delete the read list
    for( ev_list = refp->lastRead; ev_list != NULL; ev_list = ev_next ) {
        ev_next = ev_list->next;
        deleteEvent( ev_list );
        bumpCounter( &read_list_elements, -1, NULL, NULL );
    }
    refp->lastRead  = NULL;
    refp->lastWrite = newRegularEvent( current_stack_frame, pc );

    gc( );

}

static VG_REGPARM(3)
void recordCall(Addr32 sp, Addr32 ca, Addr32 ra) 
{
    StackFrame * newFrame = current_stack_frame + 1;
    TraceRec * newTR = newTraceRec( ca, mkTaggedPtr2( newFrame, TPT_OPEN ) );

    if( newFrame==stack_base+N_STACK_FRAMES ) {
        VG_(tool_panic)("Out of memory!");
    }
    bumpCounter( &stack_frames, 1, NULL, NULL );

    // Set hiddenness in caller since we do not know call address here

    checkIfHidden( current_stack_frame, ca, 1 ); // no recheck!

    // newFrame->parent      = current_stack_frame;
    newFrame->sp          = sp;
    newFrame->call_addr   = ca;
    newFrame->ret_addr    = ra;
    // newFrame->seq         = instructions;
    newFrame->flags       = 0;
    newFrame->call_header = newTR;
    newFrame->stack_mark  = smarks+topMark;

    current_stack_frame = newFrame;

    gc( );

}

static void pop_stack_frame(void)
{
   // Change call header to point to parent call header rather than stack
   // thus making it a closed call header

   if( current_stack_frame > stack_base ) {
      TraceRec * this_call = current_stack_frame[0].call_header,
               * prev_call = current_stack_frame[-1].call_header;
                 // The TR for the returning call and previous call

      this_call->link = mkTaggedPtr2( prev_call, TPT_CLOSED );
      (void) newTraceRec( 0, mkTaggedPtr2( this_call, TPT_RET ) );

      current_stack_frame--;
   }
}

static VG_REGPARM(2)
void recordRet(Addr32 sp, Addr32 target) 
{
   // Should check that we are really returning to the next stack frame
   // and in that case make it the current stack frame
   
   if( current_stack_frame < stack_base ) return;

   // the sp in the StackFrame struct is the sp *after* pushing the 
   // return address; hence adding 4 bytes should yield the sp 
   // after the return has popped

   while( current_stack_frame-1 >= stack_base && current_stack_frame[-1].sp + 4 < sp ) {
     // we need to unwind the stack
     pop_stack_frame( );
   }
   // we have found the right stack frame

   if( current_stack_frame->sp+4 > sp ) {
     // this return does not correspond to the appropriate call
     // do nothing
   } else {
     // we have returned and possibly popped a few more words off of the stack
     if( current_stack_frame->ret_addr != target ) {
       // we're not returning to the scene of the crime, sorry, call
       
     }
     // we will not need this retrun address any more
     pop_stack_frame( );
   }

   if( current_stack_frame < stack_base ) {
     VG_(tool_panic)( "Shadow stack underflow" );
   }

   recordSpChange( sp );
   gc( );

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
