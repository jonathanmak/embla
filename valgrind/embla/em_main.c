
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
   - spChange and adjust_shadow_sp are maybe not both needed
*/

//
//  Compilation options
//

#define  READ_LIST_COMPACT  1        // 0 turn off read list compaction
#define  DO_PROFILE         1
#define  PRECISE_ICOUNT     1
#define  DO_CHECK           1
#define  INSTR_LVL_DEPS     0
#define  TRACE_REG_DEPS     0
#define  RECORD_CF_EDGES    1
#define  CHECK_DIRTY_FRAGS  1
#define  GENERATIONAL_COMPACT 1

#define  DEBUG_PRINT        1
#define  EMPTY_RECORD       0
#define  DO_NOT_INSTRUMENT  0
#define  MOCK_RTENTRY       0
#define  FULL_CONTOURS      0
#define  INSTRUMENT_GC      0
#define  LIGHT_IGC          1
#define  DUMP_TRACE_PILE    0
#define  DUMP_MEMORY_MAP    0
#define  CRITPATH_ANALYSIS  1
#define  PRINT_RESULTS_TABLE 1
#define  PRINT_BB           0

// Major modes

int measure_span = 0;   // By default, collect dependencies

// Temporary definitions

unsigned interesting_address=0;


//
// Other includes and definitions
//

#include "pub_tool_vki.h"
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

#define  RESULT_ENTRIES     1000003  // smallest prime >= a million
#define  FILE_LEN               256
#define  FN_LEN                 128
#define  BUF_SIZE               512
#define  II_CHUNK_SIZE      1000000
// #define  STRING_TABLE         10007  // smallest prime >= 10000

//#define  RT_IDX_BITS                        8
#define  RT_IDX_BITS                        3
#define  RT_ENTRIES_PER_LINE (1<<RT_IDX_BITS)  // must be power of 2
#define  PRED_ENTRIES_PER_LINE 4

#define  BITS_PER_REF      0
#define  BITS_PER_FRAG     8
#define  BITS_PER_ADDRESS 32
#define  REFS_PER_FRAG    (1 << (BITS_PER_FRAG-BITS_PER_REF))
#define  FRAGS_IN_MAP     (1 << (BITS_PER_ADDRESS-BITS_PER_FRAG))
#define  FRAG_MASK        ((1 << BITS_PER_FRAG) - 1)

#define  GET_FRAG_PTR(m,a)   m[ GET_FRAG_IDX( a ) ]
#define  GET_FRAG_IDX(a)     ( (unsigned) (a) >> BITS_PER_FRAG )
#define  GET_REF_ADDR(fp,a)  ( &( fp->refs[(a&FRAG_MASK) >> BITS_PER_REF] ) )

//
// Dirty bits for the memory map
//

#define  FRAGS_PER_BIT            64
#define  BITS_PER_WORD            ( sizeof( int ) * 8 )
#define  FRAGS_PER_WORD           ( FRAGS_PER_BIT * BITS_PER_WORD )

#define  FIRST_FRAG_FOR_BIT(w,b)  ( (w) * FRAGS_PER_WORD + (b) * FRAGS_PER_BIT )
#define  WORD_OF_FRAG(f)          ( (unsigned) f / FRAGS_PER_WORD )
#define  BIT_IN_WORD(f)           ( 1 << ( ( (unsigned) f / FRAGS_PER_BIT ) % BITS_PER_WORD ) )

#define  DIRTY_WORDS              ( FRAGS_IN_MAP / FRAGS_PER_WORD )

static unsigned int * dirty_map;


//
// Debug macros
//

#if DEBUG_PRINT

#define  BONK(s) VG_(write)( 2, s, VG_(strlen)( s ) )
#define  DPRINT1(s,x)     { VG_(sprintf)( dbuf, s, x );       BONK( dbuf ); }
#define  DPRINT2(s,x,y)   { VG_(sprintf)( dbuf, s, x, y );    BONK( dbuf ); }
#define  DPRINT3(s,x,y,z) { VG_(sprintf)( dbuf, s, x, y, z ); BONK( dbuf ); }
#define  DPRINT4(s,w,x,y,z) { VG_(sprintf)( dbuf, s, w, x, y, z ); BONK( dbuf ); }

#else

#define BONK(s) { }
#define DPRINT1(s,x) { }
#define DPRINT2(s,x,y) { }
#define DPRINT3(s,x,y,z) { }
#define DPRINT4(s,w,x,y,z) { }

#endif

#if INSTRUMENT_GC
#define GCBONK( s ) BONK( s )
#else
#define GCBONK( s )
#endif

// #define IFINS(n,s) if( instructions == n ) { s; }
#define IFINS(n,s)                    // empty
#define IFINS1(s) // if( instructions == 19306337 ) { s; }

#if INSTR_LVL_DEPS
#define N_INSTR_DEPS 4
#endif

#if DO_CHECK
static void check(int c, Char *s)
{
   if( !( c ) ) {
     VG_(tool_panic)( s );
   }
}
#else
#define check(c,m) /* Nothing */
#endif


#define IFDID( c ) {if( did_gc>1 ) { c } }
static unsigned did_gc=0;

#define DF_DIRECT 0
#define DF_INDIRECT 1
#define DF_HIDDEN 2
#define DF_N_EFLAGS 3
#define DF_STACK 0
#define DF_HEAP 1
#define DF_FALSE 2
#define DF_N_RFLAGS
#define DF_MK_KEY(rf,hf,tf) (rf*DF_N_EFLAGS*DF_N_EFLAGS + hf*DF_N_EFLAGS + tf)
#define DF_GET_RFLAG(k) (k / (DF_N_EFLAGS*DF_N_EFLAGS))
#define DF_GET_HFLAG(k) ( (k/DF_N_EFLAGS) % DF_N_EFLAGS )
#define DF_GET_TFLAG(k) (k%DF_N_EFLAGS)

#define  CONT_LEN         1024

#define N_SMARKS          1000000
#define N_STACK_FRAMES    1000000
static unsigned n_trace_recs = 40000000;

#if FULL_CONTOURS
static Char h_cont[CONT_LEN], t_cont[CONT_LEN];
#endif

#define  SF_HIDDEN        1   // The function owning the frame is HIDDEN
#define  SF_STR_HIDDEN    2   // STRongly HIDDEN; callees inherit the property
#define  SF_SEEN          4   // We have SEEN this stack frame

//
// Tags with i_info != NULL
//
#define  TPT_REG                  0    // A regular node
#define  TPT_OPEN                 1    // An open call header; corresponds to a frame
#define  TPT_CLOSED               2    // A closed call header
#define  TPT_REG_OR_CLOSED_LIVE   3    // Valid from phase 1-3 in compaction

//
// Tags with i_info == NULL
//
#define  TPT_RET          0   // A return header
#define  TPT_BRIDGE       1   // Bridging a gap between live nodes
#define  TPT_RET_LIVE     2   // A live return header (important for phase 2)

#define SAVE_RESTORE_TRACKING 1


#if DO_PROFILE
#define PROFILE( c ) c
#else
#define PROFILE( c ) 
#endif

//
// For Critical Path Analysis
//
#define  N_FRAMES         1000  // Nesting level
static int dyn_only =  0;     // Only track dynamic dependencies
static int sta_only =  0;     // Only track static dependencies
static int dctl =      0;     // Track control dependencies dynamically (ignore joins)
static int draw =      0;     // Track RAW dependencies dynamically
static int dwar =      0;     // Track WAR dependencies dynamically...
static int dwaw =      0;     // Track WAW dependencies dynamically...
static int sctl =      0;     // Track control dependencies statically (consider joins)
static int sraw =      0;     // Track RAW dependencies statically
static int swar =      0;     // Track WAR dependencies statically...
static int swaw =      0;     // Track WAW dependencies statically...
static int track_stack_name_deps = 0; // ...including/except WAR/WAW dependencies on stack
static int track_hidden =   0;     // Track HIDDEN dependencies
static int para_non_calls = 0;     // Allows lines to be executed in parallel even if they don't contain function calls

#define  MASK_RAW        1
#define  MASK_WAR        2
#define  MASK_WAW        4
#define  MASK_CTL        8
#define  MASK_HIDDEN     16
#define  MASK_STACK      32

static int sample_freq =    0;
static int sample_count =   0;
static int sample_threshold = 0;

typedef enum { SR_NONE, SR_SAVE, SR_RESTORE, SR_MOV_SP } SRCode;

static struct {
    int elim_stack_alias;
} opt;

typedef unsigned long long int ICount;

typedef unsigned int StatData;

static StatData
   mem_table_frags = 0,
   stack_frames = 0,
   read_list_elements = 0,
   max_read_list_elements = 0,
   dirty_frags=0,
   all_frags=0;

static unsigned int translations = 0;
static ICount instructions = 0;

const char * trace_file_name = "embla.trace";
const char * edge_file_name = "embla.edges";
const char * dep_file_name = "embla.deps";
const char * hidden_func_file_name = "embla.hidden-funcs";

typedef
   struct _RTEntry {
     char * h_file;
     char * h_fn;
     UInt code;
     char d_inf;
     Int h_line;
     Int t_line;
     char h_inf;
     char t_inf;
     UInt n_raw, n_war, n_waw;
//     struct _TraceRec * h_tr;
//     struct _TraceRec * t_tr;
     struct _RTEntry *next;
   }
   RTEntry;

// static RTEntry mock_rtentry = {"mock", "", 0, 'o', 0, 0, 'c', 'c', 0, 0, 0, /*NULL, NULL,*/ NULL};

/********************************************************************************
 *                                                                              *
 * Statment, instruction and line info                                          *
 *                                                                              *
 ********************************************************************************/
#if RECORD_CF_EDGES
typedef struct _LineList {
   struct _LineInfo *line;
   struct _LineList *next;
} LineList;
#endif

typedef struct _LineInfo {
#if PRINT_RESULTS_TABLE
   RTEntry  *entries[RT_ENTRIES_PER_LINE];
#endif
#if RECORD_CF_EDGES
   LineList *deps;
   LineList *pred[PRED_ENTRIES_PER_LINE];
#endif
   unsigned line;
   char    *file;
   char    *func;
   struct _LineInfo *next;
} LineInfo;

#if RECORD_CF_EDGES
#if PRINT_RESULTS_TABLE
static LineInfo dummy_line_info = { { }, NULL, { }, 0, "", "", NULL };
#else
static LineInfo dummy_line_info = { NULL, { }, 0, "", "", NULL };
#endif
#else
#if PRINT_RESULTS_TABLE
static LineInfo dummy_line_info = { { }, 0, "", "", NULL };
#else
static LineInfo dummy_line_info = { 0, "", "", NULL };
#endif
#endif

#if INSTR_LVL_DEPS
typedef struct _InstrInfoList {
   struct _InstrInfo     *i_info;
   struct _InstrInfoList *next;
   UInt n_raw, n_war, n_waw;
} InstrInfoList;
#endif

typedef struct {
   Addr32   i_addr;
   unsigned i_len;
   LineInfo *line;
   struct _TraceRec *aeon;
#if INSTR_LVL_DEPS
   InstrInfoList *(i_deps[N_INSTR_DEPS]);
#endif
} InstrInfo;

#if INSTR_LVL_DEPS
#define DUMMY_II_INITIALIZER   ,{NULL,NULL,NULL,NULL}
#else
#define DUMMY_II_INITIALIZER  /* */
#endif

static InstrInfo dummy_instr_info = {1, 0, &dummy_line_info, NULL DUMMY_II_INITIALIZER};

#define SI_SAVE_REST  1
#define SI_MOV_SP     2

typedef struct {
   InstrInfo  *i_info;
   UShort      offset;
   UChar       size;
   UChar       flags;
   UShort      nElems;
   UShort      bias;
} StmInfo;

#define STM_INFO_BLOCKSIZE 10000

static StmInfo *stmInfoBlock = NULL;
static int      stmInfoIndex = STM_INFO_BLOCKSIZE;

static StmInfo *mkStmInfoI( InstrInfo *i_info, Int base, Int nElems, Int bias, 
                            UChar size, UChar flags )
{

    if( stmInfoIndex >= STM_INFO_BLOCKSIZE ) {
       stmInfoBlock = (StmInfo *) VG_(calloc)( STM_INFO_BLOCKSIZE, sizeof( StmInfo ) );
       check( stmInfoBlock != NULL, "Out of memory!" );
       stmInfoIndex = 0;
    }
    stmInfoBlock[stmInfoIndex].i_info = i_info;
    stmInfoBlock[stmInfoIndex].offset = base;
    stmInfoBlock[stmInfoIndex].size   = size;
    stmInfoBlock[stmInfoIndex].flags  = flags;
    stmInfoBlock[stmInfoIndex].nElems = nElems;
    stmInfoBlock[stmInfoIndex].bias   = bias;
    stmInfoIndex++;
    
    return stmInfoBlock + stmInfoIndex - 1;
}

static StmInfo *mkStmInfo( InstrInfo *i_info, UShort offset, UChar size, UChar flags )
{

    return mkStmInfoI( i_info, offset, 0, 0, size, flags );
}

/**********************************************************************
 * Critical path analysis
 **********************************************************************/

#if CRITPATH_ANALYSIS

typedef struct _INode {
  LineInfo *line;
  Bool callNode;
  long long int cost;
  struct _INodeSet *deps;
  long long int cpLength;
  struct _INode *cp;
  int numParents; // For Critical Path Analysis only
} INode;

typedef struct _INodeList {
  INode *node;
  struct _INodeList *next;
} INodeList;

// HashSet
#define INODE_SET_INIT_SIZE 2
typedef struct _INodeSet {
  int size;
  int numBuckets; // Must be power of 2
  INodeList **buckets;
} INodeSet;

typedef struct {
  INodeList     **table;
  int             idx_bits, 
                  n_items;
} LatestNodeTable;

static INodeList *empty_latest_node_list = NULL;

static LatestNodeTable initial_LatestNodeTable = { &empty_latest_node_list, 0, 0 };

typedef struct _FrameGraph {
  INodeList *roots;
  INode *currNode;
  INode *lastNonCallNode;
  INode *lastBranchNode; // for adding dynamic control dependencies
  INode *callerNode;
  LatestNodeTable latestNodesTbl;
} FrameGraph;

static FrameGraph *firstFrame;
static FrameGraph *currFrame;
static long long int totalCost;

static INodeList *consINode(INodeList *tail, INode *head) {
  INodeList *newList = VG_(malloc)(sizeof(INodeList));
  if (newList == NULL) {
    VG_(tool_panic)( "Out of space for INodeLists." );
  }
  newList->node = head;
  newList->next = tail;
  return newList;
}

static INodeSet *newINodeSet(void) {
  INodeSet *newSet = VG_(malloc)(sizeof(INodeSet));
  if (newSet == NULL) {
    VG_(tool_panic)( "Out of space for INodeSets." );
  }
  newSet->size=0;
  newSet->numBuckets=INODE_SET_INIT_SIZE;
  newSet->buckets = (INodeList **) VG_(calloc)(INODE_SET_INIT_SIZE, sizeof(INodeList*));
  if (newSet->buckets == NULL) {
    VG_(tool_panic)( "Out of space for INodeSet's buckets." );
  }
  return newSet;
}

static void addDep(INode *fromNode, INode *toNode) {
  INodeSet *deps = fromNode->deps;
  INodeList *iter, *last;
  int index, i;

  if (fromNode == toNode) {
    return;
  }

  index = ((int) toNode >> 2) & (deps->numBuckets-1);
  for (iter = deps->buckets[index]; iter != NULL ; iter = iter->next) {
    if (iter->node == toNode) {
      return;
    }
    last = iter;
  }

  // Node doesn't already exist - add it
  deps->size++;
  deps->buckets[index] = consINode(deps->buckets[index], toNode);
  toNode->numParents++;
  
  // Check if we need to expand hashtable

  if (deps->numBuckets < ((deps->size * 5) / 4)) {
    INodeList **oldBuckets = deps->buckets;
    int oldNumBuckets = deps->numBuckets;
    deps->numBuckets = oldNumBuckets * 2;
    deps->buckets = (INodeList **) VG_(calloc)(deps->numBuckets, sizeof(INodeList*));
    if (deps->buckets == NULL) {
      VG_(tool_panic)( "Out of space for reallocating INodeSet's buckets." );
    }
    for (i=0; i<oldNumBuckets; i++) {
      for (iter = oldBuckets[i]; iter != NULL; iter = last) {
        INode *node = iter->node;
        index = ((int) node >> 2) & (deps->numBuckets-1);
        deps->buckets[index] = consINode(deps->buckets[index], node);
        last = iter->next;
        VG_(free)(iter);
      }
    }
     VG_(free)(oldBuckets);
  }
}

static void addStaticDeps(INode *node) {
  LineList *ll; 
  INodeList **table = currFrame->latestNodesTbl.table;
  int idx_bits = currFrame->latestNodesTbl.idx_bits;
  for( ll = node->line->deps; ll != NULL; ll = ll->next ) {
     LineInfo *line = ll->line;
     int idx = ( line->line ) & ( ( 1 << idx_bits ) - 1 );
     INodeList *p = table[ idx ];

     while( p != NULL && p->node->line != line ) {
        p = p->next;
     }

     if (p != NULL) {
       addDep(p->node, node);
     }
  }
}

static void doubleLatestNodeTbl( void )
{
   LatestNodeTable *lnt = &(currFrame->latestNodesTbl);
   int i, idx_bits = lnt->idx_bits;
   INodeList **newTbl = (INodeList **) VG_(calloc)( 1 << (idx_bits+1), sizeof(INodeList *) );

   check( newTbl != NULL, "Out of memory" );

   for( i=0; i < (1 << (idx_bits+1)); i++ ) {
      newTbl[i] = NULL;
   }

   for( i=0; i < (1 << idx_bits); i++ ) {
      INodeList *q = lnt->table[i], *next;
      while( q != NULL ) {
         int n_idx = q->node->line->line & ( ( 1 << (idx_bits+1) ) - 1 );
         next = q->next;
         q->next = newTbl[ n_idx ];
         newTbl[ n_idx ] = q;
         q = next;
      }
   }
   if( lnt->table != &empty_latest_node_list ) VG_(free)( lnt->table );
   lnt->idx_bits++;
   lnt->table = newTbl;
} 

static void updateLatestNodeTbl(INode *node) {
  INodeList **table = currFrame->latestNodesTbl.table;
  int idx_bits = currFrame->latestNodesTbl.idx_bits;
  int idx;
  INodeList *p;
  LineInfo *line = node->line;

  idx = ( line->line ) & ( ( 1 << idx_bits ) - 1 );
  p = table[ idx ];

  while( p != NULL && p->node->line != line ) {
     p = p->next;
  }

  if( p == NULL ) {
     if( currFrame->latestNodesTbl.n_items + 2 > ( 1 << idx_bits ) ) {
       doubleLatestNodeTbl( );
       table = currFrame->latestNodesTbl.table;
       idx_bits = currFrame->latestNodesTbl.idx_bits;
       idx = ( line->line ) & ( ( 1 << idx_bits ) - 1 );
     }

     p = consINode(table[idx], node);
     table[idx] = p;
     currFrame->latestNodesTbl.n_items++;
  } else {
     p->node = node;
  }
}

static void clearLatestNodeTbl( void )
{
   int i, n = 1 << (currFrame->latestNodesTbl.idx_bits);
   INodeList *l,*t;

   for( i=0; i<n; i++ ) {
     for( l = currFrame->latestNodesTbl.table[i]; l != NULL; l = t ) {
        t = l->next;
        VG_(free)(l);
     }
   }
   if( currFrame->latestNodesTbl.table != &empty_latest_node_list ) VG_(free)( currFrame->latestNodesTbl.table );
   currFrame->latestNodesTbl.idx_bits = 0;
   currFrame->latestNodesTbl.n_items = 0;
   currFrame->latestNodesTbl.table = &empty_latest_node_list;
}

static INode *new_INode(LineInfo *line) {
   INode *inode = VG_(malloc)(sizeof (INode));
   if(inode == NULL) {
     VG_(tool_panic)( "Out of space for INodes." );
   }
   inode->line = line;
   inode->callNode = False; // Set to true later if we encounter a call on this line
   inode->cost = 0;
   inode->deps = newINodeSet();
   inode->cpLength = -9999999;
   inode->cp = NULL;
   inode->numParents = 0;
   if (!para_non_calls && currFrame->lastNonCallNode != NULL) {
     addDep(currFrame->lastNonCallNode, inode);
   } else {
     currFrame->roots = consINode(currFrame->roots, inode);
   }
   currFrame->currNode = inode;

   if (!dyn_only) {
     // Add all the static dependencies
     addStaticDeps(inode);
     // Update latest node table
     updateLatestNodeTbl(inode);
   }

   if (dctl && currFrame->lastBranchNode != NULL) {
     addDep(currFrame->lastBranchNode, inode);
   }

   return inode;
}

static void newNodeFrame(INode *callerNode) {
  callerNode->callNode = True;
  currFrame++;
  if (currFrame - N_FRAMES >= firstFrame) {
    VG_(tool_panic)( "Frame roots stack overflow." );
  }
  currFrame->roots = NULL;
  currFrame->currNode = NULL;
  currFrame->lastNonCallNode = NULL;
  currFrame->lastBranchNode = NULL;
  currFrame->callerNode = callerNode;
  currFrame->latestNodesTbl = initial_LatestNodeTable;
}

#define PRINT_CP(currNode, currCp, countdown) \
        for (currNode = currCp; currNode != NULL; currNode = currNode->cp) { \
          VG_(printf)("%d(%lld), ", currNode->line->line, currNode->cost); \
          if (--countdown == 0) { \
            VG_(printf)("\n"); \
            countdown = 10; \
          } \
        } \
        VG_(printf)("\n");

// NON-RECURSIVE (BREADTH FIRST) VERSION - PROBABLY SLOWER
// DESTRUCTIVE - IT FREES EVERYTHING!
//
// Calculates critical path for current frame
static long long int critPathNodes(void) {
  INodeSet *deps;
  INodeList *nodeIter, *tail, *allNodes = NULL;
  int arrCap = 100, stackSize = 0;
  INode *currNode, *currDep;
  INode **workStack = (INode **) VG_(malloc)(arrCap * sizeof(INode*));
  long long int currCpLength = -999999;
  INode *currCp = NULL;
  int i;

  tl_assert(workStack);
  // Add all roots to workStack
  for (nodeIter = currFrame->roots; nodeIter != NULL; nodeIter = tail) {
    currNode = nodeIter->node;
    // Just double checking the root hasn't got any parents
    if (currNode->numParents == 0) {
      currNode->cpLength = currNode->cost;
      currNode->cp = NULL;
      if (stackSize >= arrCap) {
        arrCap *= 2;
        workStack = VG_(realloc)(workStack, arrCap * sizeof(INode*));
        tl_assert(workStack);
      }
      workStack[stackSize] = currNode;
      stackSize++;
      allNodes = consINode(allNodes, currNode);
    }
    tail = nodeIter->next;
    VG_(free)(nodeIter);
  }

  // Keep going through workStack
  while (stackSize > 0) {
    stackSize--;
    currNode = workStack[stackSize];
    // Look at its CP
    if (currNode->cpLength > currCpLength) {
      currCpLength = currNode->cpLength;
      currCp = currNode;
    }

    deps = currNode->deps;
    for (i=0; i<deps->numBuckets; i++) {
      for (nodeIter = deps->buckets[i]; nodeIter != NULL; nodeIter = tail) {
        currDep = nodeIter->node;
//        VG_(printf)("%d->%d; ", currNode->line->line, currDep->line->line);
        if (currNode->cpLength + currDep->cost > currDep->cpLength) {
          currDep->cpLength = currNode->cpLength + currDep->cost;
          currDep->cp = currNode;
        }
        currDep->numParents--;
        if (currDep->numParents == 0) {
          if (stackSize >= arrCap) {
            arrCap *= 2;
            workStack = VG_(realloc)(workStack, arrCap * sizeof(INode*));
            tl_assert(workStack);
          }
          workStack[stackSize] = currDep;
          stackSize++;
          allNodes = consINode(allNodes, currDep);
        }
        tail = nodeIter->next;
        VG_(free)(nodeIter);
      }
    }
    VG_(free)(deps->buckets);
    VG_(free)(deps);
//    VG_(free)(currNode);
  }

  // Prints out the critical path
  if (currFrame->callerNode != NULL) {
    if (sample_freq > 0) {
      sample_count--;
      if (sample_count == 0) {
        int countdown = 10;
        VG_(printf)("%s:%d(%s)=%lld: ", currFrame->callerNode->line->file, currFrame->callerNode->line->line, currCp->line->file, currCpLength);
        PRINT_CP(currNode, currCp, countdown);
        sample_count = sample_freq;
      }
    }
    if (sample_threshold > 0 && currCpLength >= sample_threshold) {
        int countdown = 10;
        VG_(printf)("!%s:%d(%s)=%lld: ", currFrame->callerNode->line->file, currFrame->callerNode->line->line, currCp->line->file, currCpLength);
        PRINT_CP(currNode, currCp, countdown);
    }
  }

  for (nodeIter = allNodes; nodeIter != NULL; nodeIter = tail) {
    tail = nodeIter->next;
    VG_(free)(nodeIter->node);
    VG_(free)(nodeIter);
  }
  
  VG_(free)(workStack);
  clearLatestNodeTbl();
  return currCpLength;
}

static void retNode(void) {
  long long int cpLength = critPathNodes();
  currFrame->callerNode->cost += cpLength;
  currFrame--;
}

static void branchNode(void) {
  currFrame->lastBranchNode = currFrame->currNode;
}

static void newInstr(LineInfo *line) {
  INode *node = currFrame->currNode;

  // Reset field if this is a new line
  if (node != NULL && line != node->line) {
//  VG_(message)(Vg_UserMsg, "Cost of line %s:%d = %d", node->line->file, node->line->line, node->cost);
    // Finalise old node
    if (!(node->callNode)) {
      currFrame->lastNonCallNode = node;
    }

    // Starting a new node
    currFrame->currNode = NULL;
  }

  // Start a new node if field is NULL (due to either new line or new function call)
  node = currFrame->currNode;
  if (node == NULL) {
    node = new_INode(line);
    currFrame->currNode = node;
  }

  // Incrememt cost
  node->cost++;
  totalCost++;
}

static INode *getINode(void) {
  tl_assert(currFrame->currNode);
  return currFrame->currNode;
}

#endif

/********************************************************************************
 *                                                                              *
 * Other stuff                                                                  *
 *                                                                              *
 ********************************************************************************/ 



static Addr32 lowest_shadow_sp=0xffffffff, highest_shadow_sp=0;

static Char dbuf[BUF_SIZE] __attribute__((unused));

typedef unsigned TaggedPtr;

static TaggedPtr mkTaggedPtr(void *ptr, unsigned flag1, unsigned flag2)
{

   return ( (unsigned) ptr ) | ( ( flag1<<1 ) & 2 ) | ( flag2 & 1 );
}

#define mkTaggedPtr2(ptr,flag) ( ( (unsigned) (ptr) ) | ( (flag) & 3) )

#define TPTR_UNTAG( t_ptr ) ( t_ptr & ~3 )
#define ToTrP( t_ptr ) ( (TraceRec   *) TPTR_UNTAG( t_ptr ) )
#define ToStP( t_ptr ) ( (StackFrame *) TPTR_UNTAG( t_ptr ) )

#define TP_GET_FLAGS( t_ptr ) ( t_ptr & 3 )
#define TP_GET_FLAG1( t_ptr ) ( ( t_ptr >> 1 ) & 1 )
#define TP_GET_FLAG2( t_ptr ) ( t_ptr & 1 )

#define EQUAL_EVENT(x,y) ( x == y )

typedef 
   struct _TraceRec {
      InstrInfo *i_info;
      TaggedPtr  link;
#if CRITPATH_ANALYSIS
      INode *inode;
#endif
   }
   TraceRec;

static TraceRec *trace_pile;
static TraceRec *last_trace_rec;
static TraceRec *first_new_tr;

static unsigned long int n_calls_to_newTR= 0; 

static TraceRec * newTraceRec(InstrInfo *i_info, TaggedPtr link)
{
   n_calls_to_newTR++;
   if( last_trace_rec >= trace_pile + n_trace_recs ) {
     // DPRINT1( "Bailing out after %u calls\n", n_calls_to_newTR );
       VG_(tool_panic)( "Trace pile overflow" );
   }
   last_trace_rec++;
   last_trace_rec->i_info = i_info;
   last_trace_rec->link   = link;
#if CRITPATH_ANALYSIS
   last_trace_rec->inode = getINode();
#endif
   return last_trace_rec;
}

#if CRITPATH_ANALYSIS
static void addNodeDependency(TraceRec *old_tr, TraceRec *new_tr) {
   INode *oldINode = old_tr->inode;
   INode *newINode = new_tr->inode;
   addDep(oldINode, newINode);
}
#endif

typedef struct {
   Addr32    sp;
   TraceRec *tr;
} StackMark;

static StackMark smarks[N_SMARKS];
static int topMark=0;

typedef long long unsigned int TimeStamp;

typedef struct _LineInfoTimes {
  TimeStamp time;
  LineInfo *line;
  struct _LineInfoTimes *next;
} LineInfoTimes;

static LineInfoTimes *LITfree = NULL;

typedef struct {
  LineInfoTimes **table;
  int             idx_bits, 
                  n_items;
} LITTable;

static LineInfoTimes *line_info_times_ptr = NULL;

static LITTable initial_LIT_table = { &line_info_times_ptr, 0, 0 };

typedef
   struct _StackFrame {
     Addr32 sp, /* call_addr, */ ret_addr;
     unsigned int flags;
     // struct _StackFrame *parent;
     TraceRec *call_header;
     StackMark *stack_mark;
#if RECORD_CF_EDGES
     LineInfo *current_line;
#endif
     TimeStamp span;
     TimeStamp seq_span;
     LineInfo  *entry_line;
     LITTable  stamps;
   }
   StackFrame;

static StackFrame stack_base[N_STACK_FRAMES] =
       {
         // initializing the first
         {(unsigned int) -1, /* 0, */ 0, 0, NULL, smarks
#if RECORD_CF_EDGES
         , &dummy_line_info
#endif
	  , 0, 0, &dummy_line_info, { &line_info_times_ptr, 0, 0 }
         } 
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

static TaggedPtr newRegularEvent(StackFrame *frame, InstrInfo *i_info)
{
   TraceRec  *tr   = newTraceRec( i_info, mkTaggedPtr2( frame->call_header, TPT_REG ) );
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

   PROFILE( bumpCounter( &read_list_elements, 1, NULL, &max_read_list_elements ); )

   l = freeEvent;
   freeEvent = freeEvent->next;
   l->ev   = e;
   l->next = t;
   return l;
}

static EventList *copyEventList( EventList *l )
{
   EventList  *out=NULL, *p=NULL;

   if( l == NULL ) {
       return NULL;
   }

   out = p = consEvent( l->ev, NULL );
   l = l->next;

   while( l != NULL ) {
       p->next = consEvent( l->ev, NULL );
       l = l->next;
   }
   return out;
}

static void deleteEvent(EventList *l)
{
   l->next = freeEvent;
   freeEvent = l;

   PROFILE( bumpCounter( &read_list_elements, -1, NULL, NULL ); )
}

#if TRACE_REG_DEPS

typedef
   struct {
     TraceRec  *frame; 
     Event      regLastWrite;
     short      offset, size;
     UInt      *edge;
   }
   SaveDesc;

SaveDesc * saved_save_desc;
int saved_offset, *saved_edge;
Event saved_timestamp;

#endif

// If the last reference was a write, lastRead is NULL
typedef
   struct {
     EventList *lastRead;
     Event      lastWrite;
     int        offsize;      // Positive => accesssUnit, negative => subordinate
#if TRACE_REG_DEPS
     SaveDesc  *saveDesc;
#endif
   }
   RefInfo;

typedef
   struct {
     RefInfo refs[REFS_PER_FRAG];
#if CHECK_DIRTY_FRAGS
     int     flag;
#endif
   }
   MapFragment;

static MapFragment **map;

#if TRACE_REG_DEPS
typedef
   struct {
     Event lastWrite;
     int   offsize;   // Positive => access unit, negative => subordinate
   }
   RegisterInfo;

static RegisterInfo *registerMap;
static int registerMapSize;

static void validateRegisterMap( void )
{
    int i=0, j, a=0;

    do {
       int s = registerMap[i].offsize;
       if( s<=0 ) {
          DPRINT3( "Negative AU size: %d at index %d after %llu\n", s, i, instructions );
          s = 1;
          a=1;
       }
       for( j=1; j<s; j++ ) {
          if( registerMap[i+j].offsize != -j ) {
              DPRINT3( "Funny offsize %d at index %d after %llu instructions\n",
                        registerMap[i+j].offsize, j, instructions );
              a = 1;
          }
       }
       i += s;
    } while( i < registerMapSize );

    check( a==0, "Funny register map\n" );
}

static void ensureRegisterMap( int size )
{
    int i;

    if( registerMap == NULL ) {
        registerMapSize = size;
        registerMap = (RegisterInfo *) VG_(calloc)( size, sizeof( RegisterInfo ) );
        check( registerMap != NULL, "Out of memory for register map!" );
        for( i=0; i<size; i+=8 ) {
            int j;
            registerMap[i].lastWrite = mkTaggedPtr2( trace_pile+1, 0 ); // was TPT_REG
            registerMap[i].offsize   = 8;
            for( j=1; j<8; j++ ) {
                registerMap[i+j].offsize = -j;
                registerMap[i+j].lastWrite = mkTaggedPtr2( trace_pile+1, 0 ); // was TPT_REG
            }
        }
    }
}
#endif        

static Char buf[512+2*CONT_LEN];

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

typedef struct _StringEntry {
   char *str;
   struct _StringEntry *next;
} StringEntry;

static StringEntry *string_table[RESULT_ENTRIES];

static char* questions;

static char *intern_string( char *str )
{
   StringEntry **entry_p = string_table + hash( str ),
                *entry   = *entry_p;

   while( entry != NULL && VG_(strcmp)( str, entry->str ) ) {
      entry = entry->next;
   }
   if( entry==NULL ) {
      entry = (StringEntry *) VG_(calloc)( 1, sizeof( StringEntry ) );
      check( entry != NULL, "Out of memory" );
      entry->str = (char *) VG_(calloc)( VG_(strlen)( str ) + 1, sizeof( char ) );
      check( entry->str != NULL, "Out of memory" );
      VG_(strcpy)( entry->str, str );
      entry->next = *entry_p;
      *entry_p = entry;
   }
   return entry->str;
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

#if 0
static void logTranslation( Addr32 addr )
{
  Char file[FILE_LEN], func[FN_LEN];
  UInt line;

  getDebugInfo( addr, file, func, &line );

  DPRINT3( "%llu: %s %u\n", instructions, file, line );
}
#endif

static LineInfo *line_table[RESULT_ENTRIES];

static LineInfo *mk_line_info_l( char *file, unsigned line, char *func )
{
   char     *i_file, buffer[BUF_SIZE];
   LineInfo *info,**info_p;

   i_file = intern_string( file );

   VG_(sprintf)( buffer, "%d %s", line, file );
   info_p = & line_table[ hash( buffer ) ];
   info = *info_p;

   // interned file
   while( info!=NULL && ( info->line != line || info->file != i_file ) ) {
      info = info->next;
   }
   if( info == NULL ) {
      int i;
      info = (LineInfo *) VG_(calloc)( 1, sizeof(LineInfo) );
      check( info != NULL, "Out of memory for line info" );
#if PRINT_RESULTS_TABLE
      for( i=0; i<RT_ENTRIES_PER_LINE; i++ ) {
         info->entries[i] = NULL;
      }
#endif
#if RECORD_CF_EDGES
      for( i=0; i<PRED_ENTRIES_PER_LINE; i++ ) {
         info->pred[i] = NULL;
      }
      info->deps = NULL;
#endif
      info->line = line;
      info->file = i_file;
      info->func = intern_string( func == NULL ? "???" : func );
      info->next = *info_p;
      *info_p = info;
   }
   return info;
}
      
static LineInfo *mk_line_info( Addr32 i_addr )
{
   char file[FILE_LEN], func[FN_LEN];
   unsigned line;

   getDebugInfo( i_addr, file, func, &line );

   return mk_line_info_l( file, line, func );
}

static InstrInfo *ii_chunk = NULL;
static unsigned   ii_idx = II_CHUNK_SIZE;

static InstrInfo *mk_i_info(InstrInfo *curr, Addr32 i_addr, unsigned i_len)
{
   if( curr != NULL ) return curr;

   if( ii_idx == II_CHUNK_SIZE ) {
      ii_chunk = (InstrInfo *) VG_(calloc)( II_CHUNK_SIZE, sizeof( InstrInfo ) );
      check( ii_chunk != NULL, "Out of memory for ii_chunk" );
      ii_idx = 0;
   }
   
   ii_chunk[ii_idx].i_addr    = i_addr;
   ii_chunk[ii_idx].i_len     = i_len;
   ii_chunk[ii_idx].aeon      = NULL;
   ii_chunk[ii_idx].line      = mk_line_info( i_addr );

#if INSTR_LVL_DEPS
   {
       int i;

       for( i=0; i<N_INSTR_DEPS; i++ ) {
          ii_chunk[ii_idx].i_deps[i] = NULL;
       }
   }
#endif

   return ii_chunk + ii_idx++;
}


/***********************************************************************
 * Implement the needs_command_line_options for Valgrind.
 **********************************************************************/

static Bool em_process_cmd_line_option(Char* arg)
{
  VG_STR_CLO(arg, "--trace-file", trace_file_name)
  else
  VG_STR_CLO(arg, "--edge-file", edge_file_name)
  else
  VG_STR_CLO(arg, "--dep-file", dep_file_name)
  else
  VG_STR_CLO(arg, "--hidden-func-file", hidden_func_file_name)
  else
  VG_XACT_CLO(arg, "--span", measure_span)
  else
  VG_XACT_CLO(arg, "--dctl", dctl)
  else
  VG_XACT_CLO(arg, "--draw", draw)
  else
  VG_XACT_CLO(arg, "--dwar", dwar)
  else
  VG_XACT_CLO(arg, "--dwaw", dwaw)
  else
  VG_XACT_CLO(arg, "--sctl", sctl)
  else
  VG_XACT_CLO(arg, "--sraw", sraw)
  else
  VG_XACT_CLO(arg, "--swar", swar)
  else
  VG_XACT_CLO(arg, "--swaw", swaw)
  else
  VG_XACT_CLO(arg, "--track-stack-name-deps", track_stack_name_deps)
  else
  VG_XACT_CLO(arg, "--track-hidden", track_hidden)
  else
  VG_NUM_CLO(arg, "--n_trace_recs", n_trace_recs)
  else
  VG_NUM_CLO(arg, "--sample_freq", sample_freq)
  else
  VG_NUM_CLO(arg, "--sample_threshold", sample_threshold)
  else
  VG_XACT_CLO(arg, "--para-non-calls", para_non_calls)
  else
    return False;
#if 0
  tl_assert(trace_file_name);
  tl_assert(trace_file_name[0]);
#endif
  return True;
}

static void em_print_usage(void)
{  
   VG_(printf)(
"    --trace-file=<name>       store trace data in <name> [embla.trace]\n"
"    --edge-file=<name>        store control flow data in <name> [embla.edges]\n"
"    --dep-file=<name>         read dependencies for span calculation from <name> [embla.deps]\n"
"    --hidden-func-file=<name> read names of hidden functions <name> [embla.hidden-funcs]\n"
"    --span                    measure critical path instead of collecting deps\n"
"    --dctl                    track dynamic control dependencies (ignore joins)\n"
"    --draw                    track dynamic RAW dependencies\n"
"    --dwar                    track dynamic WAR dependencies\n"
"    --dwaw                    track dynamic WAW dependencies\n"
"    --sctl                    track static control dependencies (consider joins)\n"
"    --sraw                    track static RAW dependencies\n"
"    --swar                    track static WAR dependencies\n"
"    --swaw                    track static WAW dependencies\n"
"    --track-stack-name-deps   track WAR/WAW dependencies on stack\n"
"    --track-hidden            track hidden dependencies\n"
"    --n_trace_recs=<n>        maximum number of trace records allowed\n"
"    --sample_freq=<n>         frequency for outputting critical paths. 0 means never output\n"
"    --sample_threshold=<n>    output critical paths above this threshold. 0 means never output\n"
"    --para-non-calls          consider parallelism even for lines without function calls\n"
   );
}

static void em_print_debug_usage(void)
{  
}

/******************************************************
 * Stack mark handling                                *
 ******************************************************/

#if 0
static void adjust_shadow_sp(Addr32 sp)
{
    if( lowest_shadow_sp > sp ) {
        lowest_shadow_sp = sp;
    }
    if( highest_shadow_sp < sp ) {
        highest_shadow_sp = sp;
    }
}
#endif

#if EMPTY_RECORD

static VG_REGPARM(1)
void recordSpChange(Addr32 newSp)
{
}

#else

static VG_REGPARM(1)
void recordSpChange(Addr32 newSp)
{
   int i = topMark;

   if( lowest_shadow_sp > newSp ) {
       lowest_shadow_sp = newSp;
   }
   if( highest_shadow_sp < newSp ) {
       highest_shadow_sp = newSp;
   }

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

}

#endif

unsigned long long inStack_calls, inStack_iters;

static unsigned inStack( Addr32 oldAddr, TraceRec *oldTR )
{
   unsigned int i;

   PROFILE( inStack_calls++; )  // counting
   
   for( i=topMark; smarks[i].tr >= oldTR; i-- ) {
      PROFILE( inStack_iters++; )   // counting
      if( smarks[i].sp > oldAddr ) {
         return DF_FALSE;
      }
   }
   return DF_STACK;
}

/***************************************************
 * Hidden function handling                        *
 ***************************************************/

static int numHiddenFuncs;
static Char **hiddenFuncs;

static int howHidden( Addr32 addr )
{
    Char fname[FN_LEN];
    int i;

    if( VG_(get_fnname)(addr, fname, FN_LEN) ) {
        for (i=0; i<numHiddenFuncs; i++) {
          if ( ! VG_(strcmp)( fname, hiddenFuncs[i])) {
            return SF_SEEN | SF_HIDDEN | SF_STR_HIDDEN;
          }
        }
        if ( ! VG_(strncmp)( fname, "_dl", 3 ) ) {
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

#if FULL_CONTOURS

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

#endif


/**************************
 * Dumping the memory map *
 **************************/

#if DUMP_MEMORY_MAP

static void dumpMemoryMap(void)
{
   int        f,i;
   EventList *p;

   for( f = 0; f < FRAGS_IN_MAP; f++ ) {
     if( map[f] != NULL ) {
       for( i = 0; i < REFS_PER_FRAG; i++ ) {
         DPRINT1( "%x", map[f]->refs[i].lastWrite );
         for( p = map[f]->refs[i].lastRead; p != NULL; p = p->next ) {
           DPRINT1( " %x", p->ev );
         }
         BONK( "\n" );
       }
     }
   }
}

#endif


/**********************************
 * Compaction routines            *
 **********************************/

unsigned long long forward_calls, forward_iters;

static Event forward( Event e, int delta )
{
   // 'e' is part of memory table

   TraceRec *otp = NULL, *tp = ToTrP( e ); // tp now points at a trace rec in the pile
   TaggedPtr fp = tp->link;
   unsigned tp_flags = TP_GET_FLAGS( fp );

   PROFILE( forward_calls++; )   // counting

   // while( ( (1<<TPT_REG) | (1<<TPT_CLOSED) ) & ( 1<<tp_flags ) ) {
   while( tp_flags == TPT_REG || tp_flags == TPT_CLOSED ) {
      PROFILE( forward_iters++; )     // counting
      otp = tp;
      tp = ToTrP( tp->link );
      tp_flags = TP_GET_FLAGS( tp->link );
   }
   // ToTrP(fp)->link = mkTaggedPtr2( tp, TPT_REG );
   if( tp < first_new_tr ) {
      return mkTaggedPtr2( otp, TP_GET_FLAGS( e ) );
   } else {
      return mkTaggedPtr2( ToTrP( tp->link ) - delta, TP_GET_FLAGS( e ) );
   }
}

#define N_READ_TABLE_BITS 12
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

#define ToUnsigned( e ) ( e )

unsigned long long compactReads_items, compactReads_iters;

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

      PROFILE( compactReads_items++; ) // counting
      next_p = lp->next;
      for( p=readtable[ev_key]; p!=NULL; p=p->next ) {
         PROFILE( compactReads_iters++; )    // counting
         if( EQUAL_EVENT( p->ev, ev ) ) {
            found = 1;
            break;
         }
      }
      if( found && READ_LIST_COMPACT ) {
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

typedef struct _OpenCalls {
   TraceRec          *call;
   struct _OpenCalls *next;
} OpenCalls;

OpenCalls *global_open_calls = NULL;

static int numClosedSinceLast(OpenCalls *open_calls) 
{
   OpenCalls *c;
   int        n=0;

   // Find number of open calls closed since last compaction
   for( c = open_calls; c != NULL && TP_GET_FLAGS( c->call->link ) == TPT_CLOSED; c = c->next ) {
      n++;
   }
   check( c==NULL || TP_GET_FLAGS( c->call->link ) == TPT_OPEN, "Funny tag in open calls" );
   return n;
}

static OpenCalls *makeReturns(OpenCalls *open_calls, int numCalls, TraceRec *new_tr)
{
   OpenCalls *c = open_calls, *old;
   int        n;

   for( n = 0; n < numCalls; n++ ) {
      new_tr[n].link = mkTaggedPtr2( c->call, TPT_RET );
      new_tr[n].i_info = NULL;
#if CRITPATH_ANALYSIS
      new_tr[n].inode = NULL;
#endif
      old = c;
      c = c->next;
      VG_(free)( old );
   }
   return c;
}

static OpenCalls *rebuildOpenCalls( OpenCalls *open_calls, StackFrame *top, TraceRec *new_tr )
{
   StackFrame *mid;
   OpenCalls  *oc = open_calls;

   for( mid = top; mid >= stack_base && mid->call_header >= new_tr; mid-- ) {
   }
   mid++; // should point at the first new stack frame

   while( mid <= top ) {
      OpenCalls *c = (OpenCalls *) VG_(calloc)( 1, sizeof(OpenCalls) );
      check( c != NULL, "Out of memory for open calls" );
      c->call = mid->call_header;
      c->next = oc;
      oc = c;
      mid++;
   }
   return oc;
}

static OpenCalls *deleteOpenCalls( OpenCalls *open_calls )
{
   OpenCalls *p = open_calls, *q;

   while( p != NULL ) {
     q = p->next;
     VG_(free)( p );
     p = q;
   }

   return NULL;
}

typedef struct _AeonItem {
   InstrInfo        *i_info;
   TraceRec         *t_rec;
   struct _AeonItem *next;
} AeonItem;

static AeonItem *freeAeonItem = NULL;

static AeonItem *mkAeonItem( InstrInfo *i_info, TraceRec *t_rec, AeonItem *next )
{
  const int N = 1000;
  int i;
  AeonItem *tmp;

  if( freeAeonItem == NULL ) {
    freeAeonItem = (AeonItem *) VG_(calloc)( N, sizeof( AeonItem ) );
    check( freeAeonItem != NULL, "Out of memory for aeon list" );
    for( i = 1; i < N; i++ ) {
       freeAeonItem[i-1].next = freeAeonItem + i;
    }
    freeAeonItem[N-1].next = NULL;
  }

  tmp = freeAeonItem;
  freeAeonItem = tmp->next;
  tmp->i_info  = i_info;
  tmp->t_rec   = t_rec;
  tmp->next    = next;

  return tmp;
}

static AeonItem **aeon_map = NULL;

const int AE_BITS = 15;

static AeonItem *lookupAI( InstrInfo *i_info )
{

  const int N_AE = 1 << AE_BITS;
  unsigned idx = ((unsigned) i_info) / 8, i;
  AeonItem *item;
  
  if( aeon_map == NULL ) {
    aeon_map = (AeonItem **) VG_(calloc)( N_AE, sizeof( AeonItem * ) );
    check( aeon_map != NULL, "Out of memory for aeon_map" );
    for( i = 0; i < N_AE; i++ ) {
      aeon_map[i] = NULL;
    }
  }

  item = aeon_map[ idx & (N_AE-1) ];
  while( item != NULL && item->i_info != i_info ) {
    item = item->next;
  }
  if( item==NULL ) {
    aeon_map[ idx & (N_AE-1) ] = item = mkAeonItem( i_info, NULL, aeon_map[ idx & (N_AE-1) ] );
  }
  return item;
}

static TraceRec *remapTR( InstrInfo *i_info, TraceRec *eoae, TraceRec *curr )
{
   AeonItem *ai = lookupAI( i_info );

   if( 1 || ai->t_rec == NULL || ai->t_rec >= eoae ) {
     ai->t_rec = curr;
     return NULL;
   } else {
     return ToTrP( ai->t_rec->link );
   }
}

static void deleteAeonMap( void )
{
   int       i;
   AeonItem *p,*q;

   for( i = 0; i < (1 << AE_BITS); i++ ) {
     p = aeon_map[ i ];
     while( p != NULL ) {
       q = p->next;
       p->next = freeAeonItem;
       freeAeonItem = p;
       p = q;
     }
     aeon_map[ i ] = NULL;
   }
}

static TraceRec *forwardTR( TraceRec *ap, TraceRec *tp, TraceRec *ecae )
{
   TraceRec *tmp = remapTR( tp->i_info, ecae, tp );

   GCBONK( "+" );
   if( tmp == NULL ) {
      tp->link = mkTaggedPtr2( ap, TPT_REG_OR_CLOSED_LIVE );
      return ap-1;
   } else {
      tp->link = mkTaggedPtr2( tmp, TPT_REG_OR_CLOSED_LIVE );
      return ap;
   }
}
      

#if DUMP_TRACE_PILE

static void dump_trace_pile(void)
{
   TraceRec *rec;
   char     *tag;
   int       off,i_idx;

   for( rec=trace_pile; rec <= last_trace_rec; rec++ ) {
//      if (rec->i_info != NULL && TP_GET_FLAGS( rec->link ) == TPT_REG) continue;// jchm2
      DPRINT1( "%u: ", rec - trace_pile );
      if( rec->i_info != NULL ) {
         switch( TP_GET_FLAGS( rec->link ) ) {
            case TPT_REG:
               tag = "REG   ";
               off = ToTrP( rec->link ) - trace_pile;
               break;
            case TPT_OPEN:
               tag = "OPEN  ";
               off = ToStP( rec->link ) - stack_base;
               break;
            case TPT_CLOSED:
               tag = "CLOSED";
               off = ToTrP( rec->link ) - trace_pile;
               break;
            default:
               tag = "FUNNY ";
               off = ToTrP( rec->link ) - trace_pile;
               break;
         }
         i_idx = rec->i_info == &dummy_instr_info ? -1 : rec->i_info - ii_chunk;
         DPRINT4( "%s %d(0x%x, %d)", tag, i_idx, rec->i_info->i_addr, rec->i_info->i_len );
         DPRINT3( "(%s %d) %d\n", rec->i_info->line->file, rec->i_info->line->line, off );
      } else {
         switch( TP_GET_FLAGS( rec->link ) ) {
            case TPT_RET:
               tag = "RET   ";
               off = ToTrP( rec->link ) - trace_pile;
               break;
            default:
               tag = "FUNNY ";
               off = ToTrP( rec->link ) - trace_pile;
               break;

         }
         DPRINT2( "%s %d\n", tag, off );
      }
   }
}
         
#endif

static void compact(void)
{

   TraceRec   *tp,*ap,*last_open,*last_closed, *ecae, *tr_tmp, *lp;
   StackFrame *sp;
   StackMark  *mp;
   int         delta, i, j, k, l, ff, num_closed_since;
   RefInfo    *info;

   // Phase 1: Determine live trace recs and find new locations. Backwards
   // Phase 2: Update all pointers into the trace pile
   //             - Memory table
   //             - Stack marks
   //             - Shadow stack
   //             - Registers
   // Phase 3: Construct list for forwards traversal. Backwards
   // Phase 4: Relocate live trace recs. Forwards

   // Phase 1:
   GCBONK( "  Phase 1... " );
   ecae = last_trace_rec+1; // that's were the next (nonexisting) aeon starts
   tp = last_trace_rec;
   ap = last_trace_rec;
   sp = current_stack_frame;
   mp = smarks+topMark;
   while( tp >= first_new_tr ) {
      GCBONK( "." );
      // Maybe start a new aeon
      if( mp->tr >= tp ) {
        ecae = tp+1;
        GCBONK( "s" );
      }
      GCBONK( "," );
      if( tp->i_info != NULL ) {
        switch( TP_GET_FLAGS( tp->link ) ) {
          // Regular
          case TPT_REG:
            ap = forwardTR( ap, tp, ecae );
            break;

          // Open header
          case TPT_OPEN:
            tp->link = mkTaggedPtr2( ap, TPT_OPEN );
            ap--;
            // Start new aeon
            ecae = tp;
            GCBONK( "o" );
            break;

          // Closed header
          case TPT_CLOSED:
            ap = forwardTR( ap, tp, ecae );
            break;

          // Unused
          case TPT_REG_OR_CLOSED_LIVE:
            VG_(tool_panic)( "Unexpected tag in phase 1 of compaction" );
            break;
        }
      } else {
	// Return
        // If tp->link < first_new_tr (the call happened before the previous 
        // compaction) we cannot just jump to the call, but instead we 
        // must continue in order to find other returns with early calls

        // Link field stores the offset from the top
        tp->link = mkTaggedPtr2( ToTrP( tp->link ), TPT_RET_LIVE );
        tp = ToTrP( tp->link );
        check( TP_GET_FLAGS( tp->link ) == TPT_CLOSED,
               "Return header not pointing at closed call in phase 1" );
        if( tp >= first_new_tr ) {
           tr_tmp = forwardTR( ap-1, tp, ecae );
           ap = ap-1 == tr_tmp ? ap : tr_tmp;
        } else {
           // do nothing ?
           // terminate on next iter
        }
      }
      tp--;
   }
   delta = ap + 1 - first_new_tr;  // I believe this
   num_closed_since = numClosedSinceLast( global_open_calls );
   delta -= num_closed_since;

   deleteAeonMap( );

   // At this point:
   // -All trace recs less than first_new_trace remain unchanged
   // -Of the trace recs from first_new_trace forwards:
   //   -REGs and CLOSEDs within CLOSED calls remain unchanged
   //   -RETs which point to CLOSEDs within CLOSED calls remain unchanged
   //   -REGS and CLOSEDs within OPEN calls would point to what their new location would be
   //    if compaction was towards the right rather than the left, and have tag
   //    changed to TPT_REG_OR_CLOSED_LIVE
   //    Their final location would be ToTrP(link)-delta
   //   -OPENs would point to what their new location would be
   //    if compaction was towards the right rather than the left
   //    Their final location would be ToTrP(link)-delta
   //   -RETs which point to CLOSEDs within OPEN calls would still point to the
   //    same caller, but have their tag chaned to TPT_RET_LIVE
   // -Delta is the number of trace recs to be deleted, which is also the size
   //  of the hole on the left if compaction was towards the right

#if DUMP_TRACE_PILE && INSTRUMENT_GC
      GCBONK("\n");
      dump_trace_pile( );
#endif
   GCBONK( "done\n  Phase 2 (" ); 
   
   // Phase 2:
   for( sp=current_stack_frame; sp >= stack_base && sp->call_header >= first_new_tr; sp-- ) {
     check( TP_GET_FLAGS( sp->call_header->link ) == TPT_OPEN, 
            "Wrong header pointed to by sp->call_header" );
     sp->call_header = ToTrP( sp->call_header->link ) - delta;
   }
   GCBONK( "stack, " );
   for( mp=smarks; mp<=smarks+topMark; mp++ ) {
    if( mp->tr >= first_new_tr ) {
     int flag = TP_GET_FLAGS( mp->tr->link );
     InstrInfo *i_info = mp->tr->i_info;
     if( flag!=TPT_OPEN && 
         flag!=TPT_REG_OR_CLOSED_LIVE && 
         (flag!=TPT_RET_LIVE || i_info!=NULL) ) 
     { 
       // jchm2: Does this ever happen?
        VG_(tool_panic)("We've found an example!");
        mp->tr = mp==smarks ? trace_pile : (mp-1)->tr;
     } else if( flag==TPT_RET_LIVE && i_info==NULL ) {
        TraceRec * header = ToTrP( mp->tr->link );
        if (header >= first_new_tr) {
          mp->tr = ToTrP( header->link ) - delta + 1;
        } else {
          mp->tr = first_new_tr + num_closed_since - 1;
        }
     } else {
        mp->tr = ToTrP( mp->tr->link ) - delta;
     }
    }
   }
   GCBONK( "marks, " );

#if TRACE_REG_DEPS
   // need to do the registers as well ...
   // must be updated to reflect generational compaction
   for( i=0; i<registerMapSize; i++ ) {
     registerMap[i].lastWrite = forward( registerMap[i].lastWrite, delta );
   }
   GCBONK( "registers, " );
#endif

   for( k=0; k<DIRTY_WORDS; k++ ) {
     if( dirty_map[k] != 0 ) {
       for( l=0; l<BITS_PER_WORD; l++ ) {
         if( dirty_map[k] & ( 1 << l ) ) {
           ff = FIRST_FRAG_FOR_BIT(k,l);
           for( i = ff; i < ff+FRAGS_PER_BIT; i++ ) {
             if( map[i] != NULL ) {
#if CHECK_DIRTY_FRAGS
              all_frags ++;
              if( map[i]->flag != 0 || first_new_tr == trace_pile )
#endif
              {
#if CHECK_DIRTY_FRAGS
               dirty_frags += map[i]->flag;
               map[i]->flag = 0;
#endif
               j=0;
               while( j<REFS_PER_FRAG ) {
                 info = map[i]->refs + j;
                 info->lastWrite = forward( info->lastWrite, delta );
                 info->lastRead = compactReads( info->lastRead, delta );
#if TRACE_REG_DEPS
                 if( info->saveDesc != NULL ) {
                   SaveDesc *sd = info->saveDesc;
                   sd->frame = ToTrP( forward( (Event) sd->frame, delta ) );
                   sd->regLastWrite = forward( sd->regLastWrite, delta );
                 }
#endif
                 // check( info->offsize > 0, "Negative offsize in compact" );
                 j += info->offsize;
               }
             }
            }
           }
         }
       }
     }
   }
   GCBONK( "map)\n  Phase 3... " ); 

   // At this point:
   // All the smarks, stack_frames and map entries should point to the new
   // locations for each trace rec

#if DUMP_TRACE_PILE && INSTRUMENT_GC
      GCBONK("\n");
      dump_trace_pile( );
#endif

   // Phase 3:
   tp = last_trace_rec;
   lp = NULL;
   ap = tp+1;//NULL;
   while( tp >= first_new_tr ) {
      if( tp->i_info != NULL ) {
        switch( TP_GET_FLAGS( tp->link ) ) {
          // Dead trace recs
          case TPT_REG:
          case TPT_CLOSED:
            tp->link = mkTaggedPtr2( lp, TPT_BRIDGE );
            tp->i_info = NULL;
            break;

          // Open header
          case TPT_OPEN:
            // Nothing needs to be done, except updating ap and lp
            ap--;
            check( ap == ToTrP( tp->link ), "ap out of sync in phase 3 of compact" );
            lp = tp;
            break;

          // Live regular or closed header
          case TPT_REG_OR_CLOSED_LIVE:
            // Certainly a regular since it was not pointed at by return
            // Do we have to distinguish TPT_REG and TPT_CLOSED?
            ap--;
            check( ap == ToTrP( tp->link ), "ap out of sync in phase 3 of compact" );
            tp->link = mkTaggedPtr2( NULL, TPT_REG );
            lp = tp;
            break;
        }
      } else {
	// Return
        TraceRec *h_ptr = ToTrP( tp->link );
        check( TP_GET_FLAGS( tp->link ) == TPT_RET_LIVE, 
               "Return header with funny tag in phase 3" );
        // here we check that the call is not before previous compact
        if( h_ptr >= first_new_tr ) {
          check( TP_GET_FLAGS( h_ptr->link ) == TPT_REG_OR_CLOSED_LIVE,
                 "Return header target with funny tag in phase 3" );
          check( h_ptr < tp, "TPT_RET_LIVE pointing up in phase 3" );
          if( ToTrP( h_ptr->link ) != ap-2 ) {
            // This is an indirection to a set leader
            // Just skip it
          } else {
            if( h_ptr+1 != tp ) { 
              // Gap between return and matching call
              h_ptr[1].link = mkTaggedPtr2( tp, TPT_BRIDGE ); 
              h_ptr[1].i_info = NULL;                         
            }
            ap -= 2;  // We're going to allocate these (call and ret)
            check( ap == ToTrP( h_ptr->link ), "ap out of sync in phase 3 of compact" ); 
            h_ptr->link = mkTaggedPtr2( NULL, TPT_CLOSED ); 
            lp = h_ptr;
          }
        } else {
          // If this leaves a gap to first_new_tr, close it with a bridge
          if( first_new_tr + num_closed_since < tp+1 ) {
            first_new_tr[ num_closed_since ].link = mkTaggedPtr2( lp, TPT_BRIDGE ); 
            first_new_tr[ num_closed_since ].i_info = NULL;
          }
        }
        tp = h_ptr;                                     // do it, and we'll terminate next iter
      }
      tp--;
   }

   // At this point
   // -All trace recs less than first_new_trace remain unchanged
   // -Of the trace recs from first_new_trace forwards:
   //   -REGs and CLOSEDs within CLOSED calls remain unchanged
   //    EXCEPT the first non-RET (if it exists) TR in a CLOSED call in an OPEN
   //    call, which would now point to corresponding RET_LIVE TR, and have 
   //    TPT_BRIDGE tag.
   //   -The first_new_tr+num_closed_since TR would point to the old location
   //    of the next live TR
   //   -RETs which point to CLOSEDs within CLOSED calls remain unchanged
   //   -REGS and CLOSEDs within OPEN calls would point to NULL, and have the
   //    original tag restored.
   //   -OPENs would point to what their new location would be
   //    if compaction was towards the right rather than the left
   //    Their final location would be ToTrP(link)-delta
   //   -RETs which point to CLOSEDs within OPEN calls would still point to the
   //    same caller, but have their tag changed to TPT_RET_LIVE

   GCBONK( "done\n  Phase 4... " ); 
#if DUMP_TRACE_PILE && INSTRUMENT_GC
      GCBONK("\n");
      dump_trace_pile( );
#endif
   
   // Phase 4:
   // First make new returns
   global_open_calls = makeReturns( global_open_calls, num_closed_since, first_new_tr );

   GCBONK( "made returns ... " );

   tp = first_new_tr + num_closed_since;
   ap = tp;
   last_open = global_open_calls == NULL ? NULL : global_open_calls->call; 
   last_closed = NULL; // yes, it can be null since we have placed the returns
   sp = global_open_calls == NULL ? stack_base : ToStP( global_open_calls->call->link ) + 1; 
   while( tp <= last_trace_rec ) {
      if( tp->i_info != NULL ) {
        switch( TP_GET_FLAGS( tp->link ) ) {
          // Regular trace rec
          case TPT_REG:
            ap->link = mkTaggedPtr2( last_open, TPT_REG );
            ap->i_info = tp->i_info;
#if CRITPATH_ANALYSIS
            ap->inode = tp->inode;
#endif
            ap++;
            break;

          // Closed call header
          case TPT_CLOSED:
            check( last_closed == NULL, "Two TPT_CLOSED with no TPT_RET" );
            last_closed = ap;
            ap->link = mkTaggedPtr2( last_open, TPT_CLOSED );
            ap->i_info = tp->i_info;
#if CRITPATH_ANALYSIS
            ap->inode = tp->inode;
#endif
            ap++;
            break;

          // Open header
          case TPT_OPEN:
            check( last_closed == NULL, "TPT_OPEN between TPT_CLOSED and TPT_RET" );
            last_open = ap;
            ap->link = mkTaggedPtr2( sp, TPT_OPEN );
            ap->i_info = tp->i_info;
#if CRITPATH_ANALYSIS
            ap->inode = tp->inode;
#endif
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
        switch( TP_GET_FLAGS( tp->link ) ) { // needs to be changed!
          case TPT_RET_LIVE:
            check( last_closed != NULL, "last_closed == NULL at TPT_RET_LIVE" );
            ap->link = mkTaggedPtr2( last_closed, TPT_RET );
            ap->i_info = NULL;
#if CRITPATH_ANALYSIS
            ap->inode = NULL;
#endif
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

   GCBONK( "compacted ... " );
   // Rebuild the open call list
   global_open_calls = rebuildOpenCalls( global_open_calls, current_stack_frame, first_new_tr );
   first_new_tr = ap;
   GCBONK( "done\n" );
#if DUMP_TRACE_PILE && INSTRUMENT_GC
      GCBONK("\n");
      dump_trace_pile( );
#endif
   
}


#define N_LEAST 9800000
static unsigned max_use = 0,
                heap_limit = N_LEAST;

// static unsigned new_gen_size = 100000;

// Policy for generational compaction
// - Always have at least 1000 tr's between compactions
// - Allocate at least twice as many tr's as words in dirty frags
// - Minimize 2*dw+mtr/rtr where dw is words in dirty frags, 
//   mtr is number of moved trace recs and rtr is reclaimed tr's

// First simple policy: Use all available space, make a complete compaction when 
// less than half of the space is free

static void gc(void) 
{
   unsigned n_used = last_trace_rec - trace_pile;
   unsigned frag_tmp = dirty_frags;

   TraceRec  *tmp = first_new_tr;

   if (max_use <= 0) {
     max_use = n_trace_recs - 100000;
   }
   if( n_used > heap_limit ) {
#if INSTRUMENT_GC || LIGHT_IGC
      DPRINT3("[ %12llu  %8u  %8u  ", instructions, n_used, last_trace_rec - first_new_tr );
#endif
#if INSTRUMENT_GC
      BONK( "\n" );
#endif
#if DUMP_TRACE_PILE
//      dump_trace_pile( );
#endif

      did_gc++;
      compact( );
#if INSTRUMENT_GC || LIGHT_IGC
      DPRINT2("%8u %10u ]\n", last_trace_rec - tmp, REFS_PER_FRAG * ( dirty_frags - frag_tmp ) );
#endif
      heap_limit = max_use;
      // heap_limit = tmp + N_LEAST + 10 * (last_trace_rec - tmp);
      // heap_limit = 4000000 + (last_trace_rec - trace_pile);
      // heap_limit = 1 + (last_trace_rec - trace_pile);
      if( heap_limit > max_use ) {
         heap_limit = max_use;
      }
      // Generation management
      if( !GENERATIONAL_COMPACT || last_trace_rec - trace_pile > n_trace_recs / 2 ) {
         global_open_calls = deleteOpenCalls( global_open_calls );
         first_new_tr = trace_pile;
      }
#if DUMP_TRACE_PILE
      dump_trace_pile( );
#endif
#if DUMP_MEMORY_MAP
      DPRINT1( "\nGC %d\n", did_gc );
      dumpMemoryMap( );
#endif
      DPRINT1( "heap_limit = %d\n", heap_limit);
   }
}

/*********************************
 *  Result entry routines        *
 *********************************/

#if PRINT_RESULTS_TABLE
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

//  if (e1->h_tr != e2->h_tr)
//    return e1->h_tr - e2->h_tr;
//  if (e1->t_tr != e2->t_tr)
//    return e1->t_tr - e2->t_tr;

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
#endif

/********************************
 * Main updResultEntry routine  *
 ********************************/

unsigned long long updResultEntry_calls, updResultEntry_nca, updResultEntry_entry;

#if MOCK_RTENTRY

static void updResultEntry(StackFrame *curr_ctx, InstrInfo *curr_info, 
                               Event old_event,
                               Addr32 ref_addr, Event new_event, int addNodeDep)
{
   return;
}

#else

int nnn = 0;

static void XXupdResultEntry(StackFrame *curr_ctx, InstrInfo *curr_info, 
                               Event old_event,
                               Addr32 ref_addr, Event new_event, int depType)
{
   TraceRec   *old_tr = ToTrP( old_event ),
              *nca_tr = ToTrP( old_tr->link ),
              *new_tr;

   InstrInfo  *h_info, *t_info;

   UInt        h_code, t_code, r_code, code;
#if PRINT_RESULTS_TABLE
   UInt        hash_value;
   UInt        t_line;
   RTEntry    *entry;
#endif

   IFDID( nnn++; );

   PROFILE( updResultEntry_calls++; )    // counting

   // The tail (source) of the dependence is related to the old reference
   // The head (sink)   of the dependence is related to the new reference

   // Find nearest common ancestor of the new ref and the old ref in the call tree
   // h_addr will be the head address of the dependency, which is either
   //   the address of the instruction making new reference, if the new ref was 
   //     in nca, or
   //   the address of the call site in the nca

   while( TP_GET_FLAGS( nca_tr->link ) != TPT_OPEN ) {   // tag 1 is open header
       PROFILE( updResultEntry_nca++; )                  // counting
       old_tr = nca_tr;
       nca_tr = ToTrP( nca_tr->link );
   }

   // nca_tr now points to call header for NCA
   // old_tr now points to the relevant trace record in the NCA

   if( TP_GET_FLAGS( old_tr->link ) == TPT_CLOSED ) {
     // The tail is indirect
     t_code = TP_GET_FLAG1( old_event ) ? DF_HIDDEN : DF_INDIRECT;
   } else {
     t_code = DF_DIRECT;
   }
   t_info = old_tr->i_info;

   if( nca_tr != curr_ctx->call_header ) {
     // The head is indirect
     h_info = ToStP( nca_tr->link )[1].call_header->i_info;
     h_code = curr_ctx->flags & SF_HIDDEN ? DF_HIDDEN : DF_INDIRECT;
     new_tr = ToStP( nca_tr->link )[1].call_header;
   } else {
     h_info = curr_info;
     h_code = DF_DIRECT;
     new_tr = ToTrP( new_event );
   }

   if( ref_addr >= lowest_shadow_sp && ref_addr <= highest_shadow_sp ) {
      r_code = inStack( ref_addr, old_tr );
   } else {
      r_code = DF_HEAP;
   }

   if( r_code == DF_FALSE ) {
       return;
   }

   code = DF_MK_KEY( r_code, h_code, t_code );

#if CRITPATH_ANALYSIS
   if (old_tr->inode == new_tr->inode) {
     return;
   }
#endif

   // We now have all necessary info to look up the dependence

#if PRINT_RESULTS_TABLE
   t_line = t_info->line->line;
//   hash_value = ( t_line + code + (int) old_tr + (int) new_tr ) & ( RT_ENTRIES_PER_LINE-1 );
   hash_value = ( t_line + code ) & ( RT_ENTRIES_PER_LINE-1 );
   entry = h_info->line->entries[hash_value];

//   while( entry!=NULL && ( entry->t_line != t_line || entry->code != code || entry->t_tr != old_tr || entry->h_tr != new_tr ) ) {
   while( entry!=NULL && ( entry->t_line != t_line || entry->code != code ) ) {
      PROFILE( updResultEntry_entry++; )       // counting
      entry = entry->next;
   }

   if( entry == NULL ) {
       LineInfo *h_line_info = h_info->line;
       entry = (RTEntry *) VG_(calloc)( 1, sizeof(RTEntry) );
       if( entry==NULL ) {
           VG_(tool_panic)( "Out of memory" );
       }
       entry->h_file = h_line_info->file;
       entry->h_fn   = h_line_info->func;
       entry->code   = code;
       entry->d_inf  = r_code == DF_STACK ? 's' : r_code == DF_FALSE ? 'f' : 'o';
       entry->h_line = h_info->line->line;
       entry->t_line = t_line;
       entry->h_inf  = " ch"[h_code]; // maybe this is too slick
       entry->t_inf  = " ch"[t_code];
       entry->n_raw  = 0;
       entry->n_war  = 0;
       entry->n_waw  = 0;
//       entry->h_tr = new_tr;
//       entry->t_tr = old_tr;
       entry->next = h_info->line->entries[hash_value];
       h_info->line->entries[hash_value] = entry;

   }
#endif

#if CRITPATH_ANALYSIS
   // Exclude hidden dependencies?
   if (track_hidden || h_code != DF_HIDDEN || t_code != DF_HIDDEN) {
       if (draw && (depType & MASK_RAW)) {
           // RAW dependency
           addNodeDependency(old_tr, new_tr);
       } else if ((dwar && (depType & MASK_WAR)) || (dwaw && (depType & MASK_WAW))) {
           // WAR/WAW dependency

           // Exclude WAR/WAW dependencies on the stack?
           // Already excluded by DF_FALSE it seems......
           if (track_stack_name_deps || r_code != DF_STACK) {
                   addNodeDependency(old_tr, new_tr);
           }
       }
   }
#endif

#if PRINT_RESULTS_TABLE
   if (depType & MASK_RAW) entry->n_raw++;
   if (depType & MASK_WAR) entry->n_war++;
   if (depType & MASK_WAW) entry->n_waw++;
#endif

}

static void updResultEntry(StackFrame *curr_ctx, InstrInfo *curr_info, 
                               Event old_event,
                               Addr32 ref_addr, Event new_event, int depType)
{

    // IFDID( BONK( "{" ); );
    XXupdResultEntry(curr_ctx, curr_info, old_event, ref_addr, new_event, depType);
    // IFDID( BONK( "}" ); );
    // if( nnn>30 ) check( 0, "Found an exit" );



    return;
}

#endif


/***********************************
 * Save/restore discovery routines *
 ***********************************/

#if TRACE_REG_DEPS

typedef 
    union _SaveDescFreeList {
      SaveDesc                 saveDesc;
      union _SaveDescFreeList *next;
    }
    SaveDescFreeList;

SaveDescFreeList *saveDescFreeList;

static SaveDesc* freeSaveDesc( SaveDesc *sd )
{
    if( sd != NULL ) {
      // DPRINT1( "Calling freeSaveDesc with %u\n", (unsigned) sd );
      SaveDescFreeList *list = (SaveDescFreeList *) sd;
      list->next = saveDescFreeList;
      saveDescFreeList = list;
    }
    return NULL;
}

static SaveDesc* ensureSaveDesc( SaveDesc *sd )
{
    int N = 1024;

    if( sd != NULL ) return sd;

    // BONK( "." );

    if( saveDescFreeList == NULL ) {
      int i;
      // BONK( "Allocating dsfl block\n" );
      saveDescFreeList = (SaveDescFreeList *) 
                             VG_(calloc)( N, sizeof( SaveDescFreeList ) );
      if( saveDescFreeList == NULL ) {
          VG_(tool_panic)( "Out of memory!" );
      }
      for( i=0; i<N-1; i++ ) {
        saveDescFreeList[i].next = &( saveDescFreeList[i+1] );
      }
      saveDescFreeList[N-1].next = NULL;
    }
    
    sd = &( saveDescFreeList -> saveDesc );
    saveDescFreeList = saveDescFreeList->next;
    // DPRINT2( "ensureSaveDesc returns %u, new free list=%u\n",  (unsigned) sd, (unsigned) saveDescFreeList );
    return sd;
}

// Called by the instrumentation for a Store
// - 'sd'        is the old SaveDesc for that memory location
// - 'addr'      is the address at which the register is potentially saved
// - 'offset'    is the offset in the guest state array corresponding to the register
// - 'size'      is the size in bytes of the register saved
// - 'lastWrite' is an Event representing the last write (PUT) to the register
// - 'edge'      points to the counter for the dependence that we may not want to generate

static SaveDesc* 
potentialSave( SaveDesc *sd, Addr32 addr, short offset, short size, Event lastWrite,
               UInt *edge )
{
    if( addr < lowest_shadow_sp-8 || addr >= highest_shadow_sp ||
        current_stack_frame->call_header < ToTrP( lastWrite ) ) {
      return freeSaveDesc( sd );
    }

    sd = ensureSaveDesc( sd );

    sd->offset       = offset;
    sd->frame        = current_stack_frame->call_header;
    sd->size         = size;
    sd->edge         = edge;
    sd->regLastWrite = lastWrite;

    return sd;
}

// Check the typing, and what should be returned!
// Correct, the trace record returned will be used to update the state table so that,
// if we have a restore, it looks like the last time it was written was the last
// write before the save
// Call with: the save descriptor of the load (stored in saved_save_desc)
//            offset used to access the guest state
//            the size of the access
//            the trace record corresponding to the current instruction

static Event potentialRestore( SaveDesc *sd, short offset, short size, Event tr )
{
    if( sd==NULL || sd->offset != offset || sd->size != size ||
        sd->frame != current_stack_frame->call_header ) {
      return tr;
    }
    ( *( sd->edge ) ) --;
    return sd->regLastWrite;
}

#endif

/***********************************
 * Recording routines              *
 ***********************************/


static void splitAccessUnit(RefInfo *item, int size)
{
    int i,j; 

    if( item->offsize > size ) {
        for( i=size; i<item->offsize; i+=size ) {
            item[i].offsize = size;
            item[i].lastWrite = item[0].lastWrite;
            item[i].lastRead = copyEventList( item[0].lastRead );
            for( j=1; j<size; j++ ) {
                item[i+j].offsize = -j;
            }
         }
         item->offsize = size;
#if TRACE_REG_DEPS
         // also delete save descriptor
         item->saveDesc = freeSaveDesc( item->saveDesc );
#endif
     }
}

static RefInfo* getRefInfo(Addr32 addr)
{
    MapFragment *frag = GET_FRAG_PTR(map,addr);
    int          i,f;
    const int init_size = 8;

    // IFDID( BONK( "[" ); );

    if( frag==NULL ) {
        frag = (MapFragment *) VG_(calloc)(1, sizeof(MapFragment));
        if( frag==NULL ) {
            VG_(tool_panic)("Out of memory!");
        }
        PROFILE( bumpCounter( &mem_table_frags, 1, NULL, NULL ); )
        GET_FRAG_PTR(map,addr) = frag;
        for(i=0; i<REFS_PER_FRAG; i++) {
            frag->refs[i].lastWrite = mkTaggedPtr2( trace_pile+1, 0 ); // was TPT_REG
            frag->refs[i].lastRead = NULL;
            if( i%init_size == 0 ) {
               frag->refs[i].offsize = init_size;
            } else {
               frag->refs[i].offsize = - i%init_size;
            }
#if TRACE_REG_DEPS
            frag->refs[i].saveDesc = NULL;
#endif
        }
        f = GET_FRAG_IDX( addr );
        dirty_map[ WORD_OF_FRAG( f ) ] |= BIT_IN_WORD( f );
    }

#if CHECK_DIRTY_FRAGS
    frag->flag = 1;
#endif
    // IFDID( BONK( "]" ); );
    return GET_REF_ADDR(frag,addr);
}

#if EMPTY_RECORD

static VG_REGPARM(2)
void recordLoad( StmInfo *i_info, Addr32 addr )
{
}

static VG_REGPARM(2)
void recordStore( StmInfo *s_info, Addr32 addr )
{
}

static VG_REGPARM(3)
void recordCall(Addr32 sp, InstrInfo *i_info, Addr32 target) 
{
}

static VG_REGPARM(3)
void recordRet(Addr32 sp, InstrInfo *i_info, Addr32 target) 
{
}

#if CRITPATH_ANALYSIS
static VG_REGPARM(0)
void recordBranch() 
{
}
#endif

#if TRACE_REG_DEPS

static VG_REGPARM(1)
void recordPut( StmInfo *s_info )
{
}

static VG_REGPARM(1)
void recordGet( StmInfo *s_info )
{
}

static VG_REGPARM(2)
void recordGetI( StmInfo *s_info, IRExpr *exp_ix )
{
}

#endif // TRACE_REG_DEPS

#else

static int maybeSplitBlock( RefInfo *refp, Addr32 addr, int size )
{

    unsigned mask = size^(size-1);

    if( ( size&(size-1) ) == 0 && ( (mask>>1)&addr ) == 0 ) {
        // size is a power of two and addr is aligned to that power
        // implies an aligned access

        if( ( (unsigned) refp->offsize ) > ( (unsigned) size ) ) {
            // we need to split
            RefInfo *block = refp->offsize > 0 ? refp : refp + refp->offsize;
            splitAccessUnit( block, size );
        }
        return size;

    } else {
        // an unaligned access
        // there are two reasons why we get here
        // 1. the size is not a power of two
        // 2. the size does not divide the address
        // of course, both of the above could be the case
        // Ideally, we'd like to find the largest power of two that divides the address
        // and is not greater than the size

        RefInfo *block = refp->offsize > 0 ? refp : refp + refp->offsize;
        int     b_size = block->offsize;
        int     or_val = addr | size | b_size;
        int     or_gpd = or_val & ( or_val ^ (or_val-1) );

        splitAccessUnit( block, or_gpd );

//        return or_gpd - ( addr & (b_size-1) );
        return size <= b_size - ( addr & (b_size-1) ) ? size : b_size - ( addr & (b_size-1) );

    }

}

static VG_REGPARM(2)
void recordLoad(StmInfo *s_info, Addr32 addr )
{
    RefInfo     *refp;
    int          size = s_info->size;
    InstrInfo   *i_info = s_info->i_info;
#if TRACE_REG_DEPS
    int          static_sr = ( s_info->flags & SI_SAVE_REST ) != 0;
#endif
    Event        new_event;

    // BONK( "L" );
    // validateRegisterMap( );
    IFINS1( BONK( "Load\n" ) )

    if( addr == interesting_address ) {
      DPRINT3( "Loaded at %s:&u by instruction %llu.\n", 
                i_info->line->file, i_info->line->line, instructions );
      interesting_address = 0;
    }

    // save/restore info in flags; if flags&SI_SAVE_REST == 1 we have a potential restore

    refp = getRefInfo( addr );

    if( refp->offsize == size ) {
        new_event = newRegularEvent( current_stack_frame, i_info );
        refp->lastRead = consEvent( new_event, refp->lastRead );

        updResultEntry( current_stack_frame,
                                    i_info, 
                                    refp->lastWrite, 
                                    addr,
                                    new_event,
                                    MASK_RAW );
    
#if TRACE_REG_DEPS
        if( static_sr ) {
          // the load part of a potential restore
          //    pick up the save descriptor for the imminent PUT
          saved_save_desc = refp->saveDesc;
        }
#endif
    } else {

        Addr32 l_addr = addr;
        int    l_size = size;

        do { 

            // BONK( "General load... " );

            int num_bytes, i;

            // split block if larger than reference
            // if reference is unaligned we split the block as necessary
            // and prepare to iterate over the aligned fragments

            num_bytes = maybeSplitBlock( refp, l_addr, l_size );

            for( i = 0; i < num_bytes; i += refp[i].offsize ) {
                new_event = newRegularEvent( current_stack_frame, i_info );
                refp[i].lastRead = consEvent( new_event, refp[i].lastRead );

                updResultEntry( current_stack_frame,
                                            i_info, 
                                            refp[i].lastWrite, 
                                            addr,
                                            new_event,
                                            MASK_RAW );
            }

            // BONK( "done\n" );

            if( num_bytes == l_size ) break;

            l_addr += num_bytes;
            l_size -= num_bytes;
            refp = getRefInfo( l_addr );

        } while ( 1 );

    }

    gc( );
}


static VG_REGPARM(2)
void recordStore( StmInfo *s_info, Addr32 addr )
{
    int          size   = s_info->size;
    InstrInfo   *i_info = s_info->i_info;
#if TRACE_REG_DEPS
    int          static_sr = s_info->flags & SI_SAVE_REST;
#endif
    RefInfo     *refp;
    EventList   *ev_list, *ev_next;
    int         l_addr = addr, l_size = size, iters = 0;
    Event        new_event;

    // BONK( "S" );
    IFINS1( DPRINT3( "Store, addr=%u, size=%u, sr=%u\n", addr, size, static_sr ) )

    do {

        int num_bytes, i;
        iters++;

        refp = getRefInfo( l_addr );

        if( refp->offsize == l_size ) {
            // This part of the access is perfect
            num_bytes = l_size;
        } else {
            // This part of the access was smaller or larger or unaligned
            // num_bytes will be l_size unless access is unaligned

            num_bytes = maybeSplitBlock( refp, l_addr, l_size );
        }

        new_event = newRegularEvent( current_stack_frame, i_info );
        for( i = 0; i < num_bytes; i += refp[i].offsize ) {

            // If the access unit in the memory table is smaller than the access size,
            // since both access and acess unit are aligned the pieces in the 
            // access unit perfectly fill the access, hence need not be split

            // is it a WAR or a WAW?
            if( refp[i].lastRead == NULL ) {        // DONE !
                // last reference was a write: a WAW
                updResultEntry( current_stack_frame,
                                            i_info, 
                                            refp[i].lastWrite, 
                                            addr,
                                            new_event,
                                            MASK_WAW );
            } else {
                // last reference was a read: a WAR
                for( ev_list = refp[i].lastRead; ev_list!=NULL; ev_list = ev_list->next ) {
                    updResultEntry( current_stack_frame,
                                                i_info, 
                                                ev_list->ev, 
                                                addr,
                                                new_event,
                                                MASK_WAR );
                }
            }

            // last reference is now a write
            // delete the read list
            for( ev_list = refp[i].lastRead; ev_list != NULL; ev_list = ev_next ) {
                ev_next = ev_list->next;
                deleteEvent( ev_list );
            }
            refp[i].lastRead  = NULL;
            refp[i].lastWrite = mkTaggedPtr2( trace_pile+1, 0 ); // not necessary for i==0
#if TRACE_REG_DEPS
            refp[i].saveDesc = freeSaveDesc( refp[i].saveDesc );
#endif
            
        }
        refp->lastWrite = new_event;
        // merge
        for( i=refp->offsize; i<num_bytes; i++ ) {
            refp[i].offsize = -i;
        }
        refp->offsize = num_bytes;
        l_size -= num_bytes;
        l_addr += num_bytes;

    } while( l_size > 0 );

#if TRACE_REG_DEPS

    // There once was a GET (a little while ago) that saved offset,
    // timestamp and edge
    // If the GET was overlapping, so that we do not count this as a 
    // potential save, it set saved_edge to NULL

    if( static_sr && iters == 1 && saved_edge != NULL ) {
        refp->saveDesc = potentialSave( NULL, addr, saved_offset, 
                                        size, saved_timestamp, 
                                        saved_edge );
    }

#endif // TRACE_REG_DEPS

    gc( );

    // if( nnn>29 ) check( 0, "Found an exit" );

}

#if TRACE_REG_DEPS

static void splitReg( RegisterInfo *regp, int size )
{
    int i;
    RegisterInfo *base = regp->offsize < 0 ? regp + regp->offsize : regp;

    for( i = size; i < base->offsize; i += size ) {
        int j;
        for( j=1; j<size; j++ ) {
            base[i+j].offsize = -j;
            base[i+j].lastWrite = base->lastWrite;
        }
        base[i].offsize = size;
        base[i].lastWrite = base->lastWrite;
    }
    base->offsize = size;
}

static VG_REGPARM(1)
void recordPut(StmInfo *s_info)
{
    int        offset = s_info->offset;
    int        size   = s_info->size;
    InstrInfo *i_info = s_info->i_info;
    int        static_sr = s_info->flags & SI_SAVE_REST;
    int        mov_sp = s_info->flags & SI_MOV_SP;

    RegisterInfo *regp = registerMap + offset;

    // BONK( "Put\n" );
    // validateRegisterMap( );
    IFINS1( BONK( "Put..." ) )

    if( ( (unsigned) regp->offsize ) < ( (unsigned) size ) ) {
        int i;
        for( i = regp->offsize; i < size; i++ ) {
           regp[i].offsize = -i;
        }
        regp->offsize = size;
    } else if( ( (unsigned) regp->offsize ) > ( (unsigned) size ) ) {
        splitReg( regp, size );
    }

    regp->lastWrite = mov_sp ? mkTaggedPtr2( trace_pile + 1, 0 )
                             : newRegularEvent( current_stack_frame, i_info );
    
    // restore recognition
    if( static_sr && saved_save_desc != NULL ) {
        regp->lastWrite 
           = (Event)
              potentialRestore(saved_save_desc, offset, size, regp->lastWrite );
    }

    gc( );

    IFINS1( BONK( " done\n" ) )

}

static VG_REGPARM(1)
void recordGet(StmInfo *s_info)
{
    int        offset = s_info->offset;
    int        size   = s_info->size;
    InstrInfo *i_info = s_info->i_info;
    int        static_sr = s_info->flags & SI_SAVE_REST;

    RegisterInfo *regp = registerMap + offset, *p;
    RTEntry *res_entry = NULL; // Not necessary since loop always iterates

    // BONK( "Get... " );
    // validateRegisterMap( );
    IFINS1( BONK( "Get\n" ) )

    if( offset == OFFSET_x86_ESP ) return;

    if( ( (unsigned) regp->offsize ) > ( (unsigned) size ) ) {
        splitReg( regp, size );
    }

    for( p = regp; p < regp+size; p = p + p->offsize ) {
        // DPRINT1( "%d ", p->offsize );

        // check( p->offsize > 0, "Funny offsize\n" );

        // Doesn't work now - I've changed it from "RTEntry* getResultEntry(...)" 
        // to "void updResultEntry(...)
        res_entry = getResultEntry( current_stack_frame, 
                                    i_info,
                                    regp->lastWrite,
                                    0 ); // will not be part of the stack
#if PRINT_RESULTS_TABLE
        res_entry->n_raw++;
#endif
    }

    if( static_sr && regp->offsize == size ) {
        saved_offset    = offset;
        saved_timestamp = regp->lastWrite;
        saved_edge      = &( res_entry->n_raw );
    } else {
        saved_edge = NULL;
    }

    gc( );

    // BONK( "done\n" );
}

static VG_REGPARM(2)
void recordPutI(StmInfo *s_info, Int ix)
{
    int  base   = s_info->offset; // aka base
    int  nElems = s_info->nElems;
    int  bias   = s_info->bias;

    int        size   = s_info->size;
    int        offset = base + size * ( ( ix+bias ) % nElems );
    InstrInfo *i_info = s_info->i_info;
    int        static_sr = s_info->flags & SI_SAVE_REST;

    RegisterInfo *regp = registerMap + offset;

    // BONK( "PutI\n" );
    // validateRegisterMap( );
    IFINS1( BONK( "PutI\n" ) )

    if( ( (unsigned) regp->offsize ) < ( (unsigned) size ) ) {
        int i;
        for( i = regp->offsize; i < size; i++ ) {
           regp[i].offsize = -i;
        }
        regp->offsize = size;
    } else if( ( (unsigned) regp->offsize ) > ( (unsigned) size ) ) {
        splitReg( regp, size );
    }

    regp->lastWrite = newRegularEvent( current_stack_frame, i_info );
    
    // restore recognition
    if( static_sr && saved_save_desc != NULL ) {
        regp->lastWrite 
           = (Event)
              potentialRestore(saved_save_desc, offset, size, regp->lastWrite );
    }

    gc( );

}

static VG_REGPARM(2)
void recordGetI(StmInfo *s_info, Int ix)
{
    int  base   = s_info->offset; // aka base
    int  nElems = s_info->nElems;
    int  bias   = s_info->bias;

    int        size   = s_info->size;
    int        offset = base + size * ( ( ix+bias ) % nElems );
    InstrInfo *i_info = s_info->i_info;
    int        static_sr = s_info->flags & SI_SAVE_REST;

    RegisterInfo *regp = registerMap + offset, *p;
    RTEntry *res_entry = NULL; // Not necessary since loop always iterates

    // BONK( "GetI\n" );
    // validateRegisterMap( );
    IFINS1( BONK( "GetI\n" ) )


    if( ( (unsigned) regp->offsize ) > ( (unsigned) size ) ) {
        splitReg( regp, size );
    }

    for( p = regp; p < regp+size; p = p + p->offsize ) {

        // Doesn't work now - I've changed it from "RTEntry* getResultEntry(...)" 
        // to "void updResultEntry(...)
        res_entry = getResultEntry( current_stack_frame, 
                                    i_info,
                                    regp->lastWrite,
                                    0 ); // will not be part of the stack
#if PRINT_RESULTS_TABLE
        res_entry->n_raw++;
#endif
    }

    if( static_sr && regp->offsize == size ) {
        saved_offset    = offset;
        saved_timestamp = regp->lastWrite;
        saved_edge      = &( res_entry->n_raw );
    } else {
        saved_edge = NULL;
    }

    gc( );
}

#endif

static VG_REGPARM(3)
void recordCall(Addr32 sp, InstrInfo *i_info, Addr32 target) 
{
    StackFrame * newFrame = current_stack_frame + 1;
    Addr32 ca = i_info->i_addr, ra = ca + i_info->i_len;
    TraceRec * newTR = newTraceRec( i_info, mkTaggedPtr2( newFrame, TPT_OPEN ) );

    // BONK( "Call\n" );
    // validateRegisterMap( );

    if( newFrame==stack_base+N_STACK_FRAMES ) {
        VG_(tool_panic)("Out of memory!");
    }
    PROFILE( bumpCounter( &stack_frames, 1, NULL, NULL ); )

    newFrame->sp          = sp;
    newFrame->ret_addr    = ra;
    newFrame->flags       = 0;
    newFrame->call_header = newTR;
    newFrame->stack_mark  = smarks+topMark;
#if RECORD_CF_EDGES
    newFrame->current_line = NULL;
#endif
    newFrame->span        = 0;
    newFrame->seq_span    = 0;
    newFrame->entry_line  = NULL;
    current_stack_frame = newFrame;

    // Set hiddenness (recheck)
    checkIfHidden( current_stack_frame, target, 0 );
    // call trace record insertion goes here 

#if CRITPATH_ANALYSIS
    newNodeFrame(newTR->inode);
#endif
    gc( );

}

static TraceRec *pop_stack_frame(InstrInfo *i_info)
{
   // Change call header to point to parent call header rather than stack
   // thus making it a closed call header

   if( current_stack_frame > stack_base ) {
      TraceRec * this_call = current_stack_frame[0].call_header,
               * prev_call = current_stack_frame[-1].call_header;
                 // The TR for the returning call and previous call

      TraceRec *ret_tr;

      this_call->link = mkTaggedPtr2( prev_call, TPT_CLOSED );
      ret_tr = newTraceRec( 0, mkTaggedPtr2( this_call, TPT_RET ) );
#if CRITPATH_ANALYSIS
      retNode();
#endif

      current_stack_frame--;

      // return trace record insterion goes here
      return ret_tr;
   }
   return NULL;
}

static VG_REGPARM(3)
void recordRet(Addr32 sp, InstrInfo *i_info, Addr32 target) 
{
   TraceRec *tr = NULL;
    // BONK( "Ret\n" );
    // validateRegisterMap( );

   // Should check that we are really returning to the next stack frame
   // and in that case make it the current stack frame
   
   if( current_stack_frame < stack_base ) return;

   // the sp in the StackFrame struct is the sp *after* pushing the 
   // return address; hence adding 4 bytes should yield the sp 
   // after the return has popped

   while( current_stack_frame-1 >= stack_base && current_stack_frame[-1].sp + 4 < sp ) {
     // we need to unwind the stack
     tr = pop_stack_frame( i_info );
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
     tr = pop_stack_frame( i_info );
   }

   if( current_stack_frame < stack_base ) {
     VG_(tool_panic)( "Shadow stack underflow" );
   }

   recordSpChange( sp );

   gc( );

}

#if CRITPATH_ANALYSIS
static VG_REGPARM(0)
void recordBranch() 
{
   branchNode();
}
#endif

#if RECORD_CF_EDGES

static LineList * consLL( LineInfo *line, LineList *next )
{
    static LineList *free_edge = NULL;
    static int       n_free_edges = 0;
    const int        block_size = 100000;

    LineList *ll;

    if( n_free_edges == 0 ) {
        free_edge = (LineList *) VG_(calloc)( block_size, sizeof(LineList) );
        check( free_edge != NULL, "Out of memory for CF edges!\n" );
        n_free_edges = block_size;
    }
    ll = free_edge;
    free_edge++;
    n_free_edges--;

    ll->next = next;
    ll->line = line;

    return ll;
}


static VG_REGPARM(1)
void recordEdge( LineInfo *line ) // Need to know line, the line of the current instruction
{
    LineList *ll, **llp;
    LineInfo *prev_line = current_stack_frame->current_line;

#if CRITPATH_ANALYSIS
    newInstr(line);
#endif

    if( line == prev_line ) return;
    current_stack_frame->current_line = line;
    if( prev_line == NULL ) return;
    
    llp = &( line->pred[prev_line->line & (PRED_ENTRIES_PER_LINE-1)] );
    for( ll = *llp; ll != NULL && ll->line != prev_line; ll = ll->next ) ;

    if( ll == NULL ) {
        *llp = consLL( prev_line, *llp );
    }
}

#endif

#endif

//
// Utility functions for emitting assignments and dirty helper calls
//

static IRExpr* emitIRAssign(IRSB *bbOut, IRExpr *exp)
{
    IRTemp t = newIRTemp( bbOut->tyenv, Ity_I32 );
    addStmtToIRSB( bbOut, IRStmt_WrTmp( t, exp ) );
    return IRExpr_RdTmp( t );
}

static void emitDC_1(IRSB *bbOut, HChar *hname, void *haddr, IRExpr *exp1)
{
    IRExpr **args = mkIRExprVec_1( exp1 );
    IRDirty *dy = unsafeIRDirty_0_N( 1, hname, haddr, args );
    addStmtToIRSB( bbOut, IRStmt_Dirty(dy) );
}

static void emitDC_2(IRSB *bbOut, HChar *hname, void *haddr, IRExpr *exp1, IRExpr *exp2)
{
    IRExpr **args = mkIRExprVec_2( exp1, exp2 );
    IRDirty *dy = unsafeIRDirty_0_N( 2, hname, haddr, args );
    addStmtToIRSB( bbOut, IRStmt_Dirty(dy) );
}

static void emitDC_3(IRSB *bbOut, HChar *hname, void *haddr, IRExpr *exp1, IRExpr *exp2,
                     IRExpr *exp3)
{
    IRExpr **args = mkIRExprVec_3( exp1, exp2, exp3 );
    IRDirty *dy = unsafeIRDirty_0_N( 3, hname, haddr, args );
    addStmtToIRSB( bbOut, IRStmt_Dirty(dy) );
}

static IRExpr* const32( unsigned val )
{
    return IRExpr_Const( IRConst_U32( val ) );
}

//
// Code instrumentation functions
//

#if 0
static int sizeofIRExpr( IRSB *bb, IRExpr *exp )
{
    return sizeofIRType( typeOfIRExpr( bb->tyenv, exp ) );
}
#endif

static void instrumentLoad( IRSB *bbOut, InstrInfo *i_info, IRExpr *exp_addr, 
                            UChar size, UChar flags )
{
    StmInfo *s_info = mkStmInfo( i_info, 0, size, flags );
    IRExpr *exp_si = const32( (UInt) s_info );

    emitDC_2( bbOut, "recordLoad", &recordLoad, exp_si, exp_addr );
}

static void instrumentStore( IRSB *bbOut, InstrInfo *i_info, IRExpr *exp_addr, UChar size,
                             UChar flags )
{
    StmInfo *s_info = mkStmInfo( i_info, 0, size, flags );
    IRExpr  *exp_si = const32( (UInt) s_info );
    emitDC_2( bbOut, "recordStore", &recordStore, exp_si, exp_addr );
}

#if TRACE_REG_DEPS

static void instrumentGet( IRSB *bbOut, InstrInfo *i_info, int offset, int size, UChar flags )
{
    StmInfo *s_info = mkStmInfo( i_info, offset, size, flags );
    IRExpr *exp_si  = const32( (UInt) s_info );
    emitDC_1( bbOut, "recordGet", &recordGet, exp_si );
}

static void instrumentPut( IRSB *bbOut, InstrInfo *i_info, int offset, int size, UChar flags )
{
    StmInfo *s_info = mkStmInfo( i_info, offset, size, flags );
    IRExpr *exp_si  = const32( (UInt) s_info );
    emitDC_1( bbOut, "recordPut", &recordPut, exp_si );
}

static void instrumentGetI( IRSB *bbOut, InstrInfo *i_info, IRExpr *exp, Int size, UChar flags )
{
    IRArray    *arr = exp->Iex.GetI.descr;
    int        bias = exp->Iex.GetI.bias;
    StmInfo *s_info = mkStmInfoI( i_info, arr->base, arr->nElems, bias, size, flags );
    IRExpr *exp_si  = const32( (UInt) s_info );

    emitDC_2( bbOut, "recordGetI", &recordGetI, exp_si, exp->Iex.GetI.ix );
}

static void instrumentPutI( IRSB *bbOut, InstrInfo *i_info, IRStmt *stm, Int size, UChar flags )
{
    IRArray    *arr = stm->Ist.PutI.descr;
    int        bias = stm->Ist.PutI.bias;
    StmInfo *s_info = mkStmInfoI( i_info, arr->base, arr->nElems, bias, size, flags );
    IRExpr *exp_si  = const32( (UInt) s_info );

    emitDC_2( bbOut, "recordPutI", &recordPutI, exp_si, stm->Ist.PutI.ix );
}

#endif

// untility function for emitting code to increment a global variable

#if 0

static void emitIncrementGlobal(IRSB *bbOut, void *addr, unsigned int amount)
{
    IRTemp tmp_instrs_old = newIRTemp( bbOut->tyenv, Ity_I32 );
    IRTemp tmp_instrs_new = newIRTemp( bbOut->tyenv, Ity_I32 );
    IRExpr *exp_instrs_new = IRExpr_RdTmp( tmp_instrs_new );

    IRExpr *exp_instr_addr = IRExpr_Const( IRConst_U32( (UInt) addr ) );
    IRExpr *exp_instrs = IRExpr_Load( Iend_LE, Ity_I32, exp_instr_addr );
    IRExpr *exp_amount = IRExpr_Const( IRConst_U32( amount ) );
    IRExpr *exp_add = IRExpr_Binop( Iop_Add32, IRExpr_RdTmp( tmp_instrs_old ), exp_amount );

    addStmtToIRSB( bbOut, IRStmt_WrTmp( tmp_instrs_old, exp_instrs ) );
    addStmtToIRSB( bbOut, IRStmt_WrTmp( tmp_instrs_new, exp_add ) );
    addStmtToIRSB( bbOut, IRStmt_Store( Iend_LE, exp_instr_addr, exp_instrs_new ) );
}

#else

static void emitIncrementGlobal(IRSB *bbOut, void *addr, unsigned int amount)
{
    IRExpr *exp_load = IRExpr_Load( Iend_LE, Ity_I32, const32( (UInt) addr ) );
    IRExpr *exp_ocnt = emitIRAssign( bbOut, exp_load );
    IRExpr *exp_add  = IRExpr_Binop( Iop_Add32, exp_ocnt, const32( amount ) );
    IRExpr *exp_ncnt = emitIRAssign( bbOut, exp_add );
    addStmtToIRSB( bbOut, IRStmt_Store( Iend_LE, const32( (UInt) addr ), exp_ncnt ) );
}

#endif

static void emitSpChange(IRSB *bbOut)
{
    HChar *hname = "recordSpChange";
    void  *haddr = &recordSpChange;

    IRTemp tmp_sp = newIRTemp(bbOut->tyenv, Ity_I32);
    IRExpr *exp_sp = IRExpr_RdTmp( tmp_sp );
    IRExpr *exp_get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );
    IRExpr **args = mkIRExprVec_1( exp_sp );
    IRDirty *dy = unsafeIRDirty_0_N( 1, hname, haddr, args );
    
    addStmtToIRSB( bbOut, IRStmt_WrTmp( tmp_sp, exp_get_sp ) );
    addStmtToIRSB( bbOut, IRStmt_Dirty(dy) );
}



// instrumentExit is called when we find and Exit in the block AND for the
// implicit Exit at the end of each block

static void instrumentExit(IRSB *bbOut, IRJumpKind jk, InstrInfo *i_info, IRExpr *tgt,
                           unsigned int loc_instr, IRExpr *guard)
{
    switch( jk ) {

      case Ijk_Call: 
          {
             HChar *hname = "recordCall";
             void  *haddr = &recordCall;
             IRExpr *exp_sp = emitIRAssign( bbOut, IRExpr_Get(OFFSET_x86_ESP, Ity_I32) );
             IRExpr *exp_tgt = emitIRAssign( bbOut, tgt );
             emitDC_3( bbOut, hname, haddr, exp_sp, const32( (UInt) i_info ), exp_tgt );
          }
          break;

      case Ijk_Ret:
          {  
             HChar *hname = "recordRet";
             void  *haddr = &recordRet;
             IRTemp tmp_sp = newIRTemp(bbOut->tyenv, Ity_I32);
             IRTemp tmp_pc = newIRTemp(bbOut->tyenv, Ity_I32);
             IRExpr *exp_sp = IRExpr_RdTmp( tmp_sp );
             IRExpr *exp_pc = IRExpr_RdTmp( tmp_pc );
             IRExpr *exp_get_sp = IRExpr_Get( OFFSET_x86_ESP, Ity_I32 );
             IRExpr **args = mkIRExprVec_3( exp_sp, const32( (UInt) i_info), exp_pc );
             IRDirty *dy = unsafeIRDirty_0_N( 3, hname, haddr, args );
             
             if( tgt==NULL ) {
                VG_(tool_panic)("Ret not at end of BB!");
             }

             addStmtToIRSB( bbOut, IRStmt_WrTmp( tmp_sp, exp_get_sp ) );
             addStmtToIRSB( bbOut, IRStmt_WrTmp( tmp_pc, tgt ) );
             addStmtToIRSB( bbOut, IRStmt_Dirty(dy) );
          }
          break;

      default: 
#if CRITPATH_ANALYSIS
          if (dctl && guard != NULL) {
             HChar *hname = "recordBranch";
             void  *haddr = &recordBranch;
             IRExpr **args = mkIRExprVec_0();
             IRDirty *dy = unsafeIRDirty_0_N( 0, hname, haddr, args );
             addStmtToIRSB( bbOut, IRStmt_Dirty(dy) );
          }
#endif
          break;
    }
}


/**********************************************
 * Instrument save/restore (static detection) *
 **********************************************/

int stack_modified = 1;

#if TRACE_REG_DEPS

static int tempUsed( IRExpr *expr, IRTemp t )
{
    int i;

    switch( expr->tag ) {
      case Iex_Get:   return 0;
      case Iex_GetI:  return 0;
      case Iex_RdTmp:   return t == expr->Iex.RdTmp.tmp;
      case Iex_Binop: return tempUsed( expr->Iex.Binop.arg1, t ) 
                          || tempUsed( expr->Iex.Binop.arg2, t );
      case Iex_Unop:  return tempUsed( expr->Iex.Unop.arg, t );
      case Iex_Load:  return tempUsed( expr->Iex.Load.addr, t );
      case Iex_Const: return 0;
      case Iex_Mux0X: return tempUsed( expr->Iex.Mux0X.cond, t)
                          || tempUsed( expr->Iex.Mux0X.expr0, t)
                          || tempUsed( expr->Iex.Mux0X.exprX, t);
      case Iex_CCall: for( i=0; expr->Iex.CCall.args[i] != NULL; i++ ) {
                          if( tempUsed( expr->Iex.CCall.args[i], t ) ) {
                              return 1;
                          }
                      }
                      return 0;
      case Iex_Binder: return 0; // Can not happen!
    }
    return 0; // Unreachable!
}

// Called with a BB 'b', an index 'i' into 'b', a temp 't' that is target of an assign,
// an SRCode 's' that is SR_SAVE if the caller has seen a GET and SR_RESTORE if the
// caller has seen a LOAD (at index 'i' of 'b'), and the size of the GET or LOAD.
// Returns the index of the matching PUT or STORE or 0 if no match.

// We need to deal with SR_MOV_SP as well!!!

static int sr_check( IRSB* bbIn, int bbIn_idx, IRTemp t, SRCode sr_code, int size )
{
    int          i;
    IRStmt  *stIn;
    IRExpr *tmpExp;
    int     offset=0;
    int     state = sr_code == SR_SAVE ? 1 : sr_code == SR_MOV_SP ? 5 : 2;

    // state==1 -> seen GET/GETI, looking for STORE to make a save
    // state==2 -> seen LOAD, looking for PUT/PUTI to make a restore
    // state==3 -> seen GET/GETI and STORE (a save), looking for end
    // state==4 -> seen LOAD and PUT/PUTI (a restore), looking for end
    // state==5 -> seen GET, looking for PUT to make sp move
    // state==6 -> seen GET and PUT (an sp move), looking for end

    for( i = bbIn_idx+1; i < bbIn->stmts_used; i++ ) {
        stIn = bbIn->stmts[i];
        switch( stIn->tag ) {
          case Ist_IMark:
              return ( state==3 || state==4 || state==6 ) ? offset : 0;
              break;
          case Ist_NoOp:
              break;
          case Ist_AbiHint:
              break;
          case Ist_Put:
              tmpExp = stIn->Ist.Put.data;
              if( state==2 && tmpExp->tag == Iex_RdTmp && tmpExp->Iex.RdTmp.tmp == t
                  && sizeofIRType( typeOfIRExpr( bbIn->tyenv, tmpExp ) ) == size ) {
                  state = 4;
                  offset = i;
              } else if( state==5 && tmpExp->tag == Iex_RdTmp && tmpExp->Iex.RdTmp.tmp == t
                     && sizeofIRType( typeOfIRExpr( bbIn->tyenv, tmpExp ) ) == size ) {
                  state = 6;
                  offset = i;
              } else if( tempUsed( tmpExp, t ) ) {
                  return 0;
              }
              break;
          case Ist_PutI:
              tmpExp = stIn->Ist.PutI.data;
              if( state==2 && tmpExp->tag == Iex_RdTmp && tmpExp->Iex.RdTmp.tmp == t 
                  && sizeofIRType( typeOfIRExpr( bbIn->tyenv, tmpExp ) ) == size ) {
                  state = 4;
                  offset = i;
              } else if( tempUsed( tmpExp, t ) ) {
                  return 0;
              }
              break;
          case Ist_WrTmp:
              if( tempUsed( stIn->Ist.WrTmp.data, t ) ) {
                  return 0;
              }
              break;
          case Ist_Store:
              tmpExp = stIn->Ist.Store.data;
              if( state==1 && tmpExp->tag == Iex_RdTmp && tmpExp->Iex.RdTmp.tmp == t 
                  && sizeofIRType( typeOfIRExpr( bbIn->tyenv, tmpExp ) ) == size ) {
                  state = 3;
                  offset = i;
              } else if( tempUsed( tmpExp, t ) || tempUsed( stIn->Ist.Store.addr, t ) ) {
                  return 0;
              }
              break;
          case Ist_Dirty:
              // this is slightly unsafe, overestimating what is a save or restore
              // maybe we should just return false here?
#if 0
              for( tmpExp = stIn->Ist.Dirty.details[0]; tmpExp != NULL; tmpExp++ ) {
                  if( tempUsed( tmpExp, t ) ) {
                      return 0;
                  }
              }
#else
              return 0;
#endif
              break;
          case Ist_MBE:
              break;
          case Ist_Exit:
              if( tempUsed( stIn->Ist.Exit.guard, t ) ) {
                  return 0;
              }
              break;
        }
    }
    return ( state==3 || state==4 || state==6) ? offset : 0;
}

#endif

static LineInfoTimes* newLineInfoTimes(void)
{
  int LITblocksize = 1000;
  LineInfoTimes *p;

  if( LITfree == NULL ) {
     int i;
     LITfree = (LineInfoTimes *) VG_(calloc)( LITblocksize, sizeof( LineInfoTimes ) );
     check( LITfree != NULL, "Out of memory for LITblock" );
     for( i=0; i < LITblocksize-1; i++ ) {
        LITfree[i].next = LITfree + i + 1;
     }
     LITfree[ LITblocksize-1 ].next = NULL;
  }
  p = LITfree;
  LITfree = LITfree->next;
  return p;
}

static void freeLineInfoTimes( LineInfoTimes *p )
{
  p->next = LITfree;
  LITfree = p;
}

static void clearLITTable( LITTable *lit )
{
   int i, n = 1 << lit->idx_bits;
   LineInfoTimes *l,*t;

   for( i=0; i<n; i++ ) {
     for( l = lit->table[i]; l != NULL; l = t ) {
        t = l->next;
        freeLineInfoTimes( l );
     }
   }
   if( lit->table != &line_info_times_ptr ) VG_(free)( lit->table );
   lit->idx_bits = 0;
   lit->n_items = 0;
   lit->table = &line_info_times_ptr;
}

static TimeStamp getTimeStamp( LITTable *lit, LineInfo *line )
{
  LineInfoTimes **table = lit->table;
  int idx_bits = lit->idx_bits;
  int idx = ( line->line ) & ( ( 1 << idx_bits ) - 1 );
  LineInfoTimes *p = table[ idx ];

  while( p != NULL && p->line != line ) {
     p = p->next;
  }

  // DPRINT3( "GET %s %u %llu\n", line->file, line->line, p==NULL ? 0 : p->time );

  return p==NULL ? 0 : p->time;
}

static void doubleLITtable( LITTable *lit )
{
   int i, idx_bits = lit->idx_bits;
   LineInfoTimes **qable 
              = (LineInfoTimes **) VG_(calloc)( 1 << (idx_bits+1), sizeof(LineInfoTimes *) );

   check( qable != NULL, "Out of memory" );

   for( i=0; i < (2 << idx_bits); i++ ) {
      qable[i] = NULL;
   }

   for( i=0; i < (1 << idx_bits); i++ ) {
      LineInfoTimes *q = lit->table[i], *next;
      while( q != NULL ) {
         int n_idx = q->line->line & ( ( 1 << (idx_bits+1) ) - 1 );
         next = q->next;
         q->next = qable[ n_idx ];
         qable[ n_idx ] = q;
         q = next;
      }
   }
   if( lit->table != &line_info_times_ptr ) VG_(free)( lit->table );
   lit->idx_bits++;
   lit->table = qable;
} 

static void setTimeStamp( LITTable *lit, LineInfo *line, TimeStamp time )
{
  LineInfoTimes **table = lit->table;
  int idx_bits = lit->idx_bits;
  int idx;
  LineInfoTimes *p;

  // DPRINT3( "SET %s %u %llu\n", line->file, line->line, time );

  idx = ( line->line ) & ( ( 1 << idx_bits ) - 1 );
  p = table[ idx ];

  while( p != NULL && p->line != line ) {
     p = p->next;
  }

  if( p == NULL ) {
     if( lit->n_items + 2 > ( 1 << idx_bits ) ) {
       doubleLITtable( lit );
       table = lit->table;
       idx_bits = lit->idx_bits;
       idx = ( line->line ) & ( ( 1 << idx_bits ) - 1 );
     }
     p = newLineInfoTimes( );

     p->next = table[ idx ];
     p->line = line;
     table[ idx ] = p;
     lit->n_items++;
  }

  p->time = time;
}

static VG_REGPARM(2)
void recordInstr(InstrInfo *i_info, Word n)  // n is the number of skipped instructions
{

    instructions += n+1;
    current_stack_frame->seq_span += n+1;

    // if( i_info->line->file != questions ) {
    //    DPRINT2( "E   %s %u\n", i_info->line->file, i_info->line->line );
    // }

    if( i_info->line == current_stack_frame->current_line ) {
       current_stack_frame->span     += n+1;
    } else {
       LineList *ll;
       StackFrame *sp = current_stack_frame;
       TimeStamp max_t = 0;

       setTimeStamp( &( sp->stamps ), sp->current_line, sp->span+n );
       for( ll = i_info->line->deps; ll != NULL; ll = ll->next ) {
          TimeStamp t = getTimeStamp( &( sp->stamps ), ll->line );
          if( t > max_t ) max_t = t;
       }
       sp->current_line = i_info->line;
       sp->span = max_t+1; // We've started executing a new instruction
    }
}

static VG_REGPARM(3)
void recordCall_span( InstrInfo *i_info, Addr32 sp, Addr32 target )
{
    StackFrame * newFrame = current_stack_frame + 1;
    Addr32 ca = i_info->i_addr, 
           ra = ca + i_info->i_len;

    // BONK( "Call\n" );

    if( newFrame==stack_base+N_STACK_FRAMES ) {
        VG_(tool_panic)("Out of memory!");
    }
    PROFILE( bumpCounter( &stack_frames, 1, NULL, NULL ); )

    newFrame->sp          = sp;
    newFrame->ret_addr    = ra;
    newFrame->flags       = 0;
    newFrame->call_header = NULL;
    newFrame->stack_mark  = smarks+topMark;
#if RECORD_CF_EDGES
    newFrame->current_line = mk_line_info( target ); 
#endif
    newFrame->span        = 0;
    newFrame->seq_span    = 0;
    newFrame->entry_line  = newFrame->current_line; // mk_line_info( target ); NEW!!!
    newFrame->stamps      = initial_LIT_table;
    current_stack_frame = newFrame;
}

static TimeStamp computeSpan( StackFrame *sp ) 
{
   int i, n = 1 << sp->stamps.idx_bits;
   LineInfoTimes **tab = sp->stamps.table;
   TimeStamp max = 0;

   for( i = 0; i < n; i++ ) {
      LineInfoTimes *ll;
      for( ll = tab[i]; ll != NULL; ll = ll->next ) {
         if( ll->time > max ) max = ll->time;
      }
   }
   if( sp->span > max ) max = sp->span;

   return max;
}

typedef struct _SPTEntry {
   TimeStamp seq, par;
   LineInfo  *line;
   struct _SPTEntry *next;
} SPTEntry;

#define SEQ_PAR_TABLE_BITS 12

static SPTEntry *seq_par_table[ 1<<SEQ_PAR_TABLE_BITS ];

static void addSeqParSpan( LineInfo *line, TimeStamp seq, TimeStamp par )
{
   int idx = (unsigned) line & ( ( 1 << SEQ_PAR_TABLE_BITS ) - 1 );
   SPTEntry *ll;

   // DPRINT3( "Add (%d) %llu %llu\n", line->line, seq, par );

   for( ll = seq_par_table[ idx ]; ll != NULL && ll->line != line; ll = ll->next ) ;

   if( ll == NULL ) {
     // First time this function
     ll = (SPTEntry *) VG_(calloc)( 1, sizeof( SPTEntry ) );
     check( ll != NULL, "Out of memory" );
     ll->seq = 0;
     ll->par = 0;
     ll->line = line;
     ll->next = seq_par_table[ idx ];
     seq_par_table[ idx ] = ll;
   }
   ll->seq += seq;
   ll->par += seq - par;
}

static void pop_stack_frame_span(void)
{
   // Find the maximum span in the current stack frame

   if( current_stack_frame > stack_base ) {
      TimeStamp     span = computeSpan( current_stack_frame );

      addSeqParSpan( current_stack_frame->entry_line, current_stack_frame->seq_span, span );
      clearLITTable( &( current_stack_frame->stamps ) );

      current_stack_frame--;
      current_stack_frame->span     += span;
      current_stack_frame->seq_span += span;
   }
}

static VG_REGPARM(2)
void recordRet_span( Addr32 sp, Addr32 target ) 
{

   // Should check that we are really returning to the next stack frame
   // and in that case make it the current stack frame
   
   if( current_stack_frame < stack_base ) return;

   // the sp in the StackFrame struct is the sp *after* pushing the 
   // return address; hence adding 4 bytes should yield the sp 
   // after the return has popped
   // This logic is x86 specific!

   while( current_stack_frame-1 >= stack_base && current_stack_frame[-1].sp + 4 < sp ) {
     // we need to unwind the stack
     pop_stack_frame_span( );
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
     pop_stack_frame_span( );
   }

   if( current_stack_frame < stack_base ) {
     VG_(tool_panic)( "Shadow stack underflow" );
   }

}


static void instrumentExit_span( IRSB *bbOut, InstrInfo *i_info, IRJumpKind jk, IRExpr *dst )
{

    switch( jk ) {

       case Ijk_Call :
            {
              IRExpr *exp_ii = const32( (UInt) i_info );
              IRExpr *exp_sp = emitIRAssign( bbOut, IRExpr_Get(OFFSET_x86_ESP, Ity_I32) );
              emitDC_3( bbOut, "recordCall_span", &recordCall_span, exp_ii, exp_sp, dst );
              break;
            }
       case Ijk_Ret  :
            {
              IRExpr *exp_sp = emitIRAssign( bbOut, IRExpr_Get(OFFSET_x86_ESP, Ity_I32) );
              emitDC_2( bbOut, "recordRet_span", &recordRet_span, exp_sp, dst );
              break;
            }
       default       : ;
          /* do nothing */
    }

}

static IRSB* em_instrument_span(VgCallbackClosure* closure,
                                IRSB* bbIn, VexGuestLayout* layout,
			        VexGuestExtents * vge,
                                IRType gWordTy, IRType hWordTy)
{
    IRSB     *bbOut = deepCopyIRSBExceptStmts( bbIn );
    int       i_idx = 0;
    Addr32    guestIAddr;
    unsigned  guestILen;
    InstrInfo *currII = NULL;

    while( i_idx<bbIn->stmts_used && bbIn->stmts[i_idx]->tag != Ist_IMark ) {
       addStmtToIRSB( bbOut, bbIn->stmts[i_idx] );
       i_idx++;
    }
    for( ; i_idx < bbIn->stmts_used; i_idx ++ ) {
       IRStmt *stIn = bbIn->stmts[i_idx];

       switch( stIn->tag ) {

          case Ist_IMark: 
              guestIAddr = (Addr32) stIn->Ist.IMark.addr;
              guestILen  = stIn->Ist.IMark.len;

              currII = mk_i_info( NULL, guestIAddr, guestILen );
	      emitDC_2( bbOut, "recordInstr", &recordInstr, 
                        const32( (UInt) currII ), const32( 0 ) );

              break;

          case Ist_Exit:
              instrumentExit_span( bbOut, currII, stIn->Ist.Exit.jk, 
                                   IRExpr_Const( stIn->Ist.Exit.dst ) );
              break;
    
          default : ; /* do nothing */
       }
       addStmtToIRSB( bbOut, stIn );
    }
    instrumentExit_span( bbOut, currII, bbOut->jumpkind, bbOut->next );

    return bbOut;
}

extern UInt vex_printf ( HChar *format, ... );

static IRSB* em_instrument_deps(VgCallbackClosure* closure,
                                IRSB* bbIn, VexGuestLayout* layout,
			        VexGuestExtents * vge,
                                IRType gWordTy, IRType hWordTy)
{
    IRSB      *bbOut;
    int       bbIn_idx;
    IRStmt    *stIn;
    Addr32    guestIAddr = 0;
    Int       guestILen  = 0;
    unsigned int loc_instr = 0;
    InstrInfo *currII=NULL;
    SRCode     sr_code = SR_NONE;
    int        sr_index = -1; 
    IRExpr    *tmpExpr;
    int       ref_size;
#if TRACE_REG_DEPS
    int       offset;
#endif
    UChar     flags = 0;

#if TRACE_REG_DEPS
    ensureRegisterMap( layout->total_sizeB );
#endif

    translations++;

    // BONK( "em_instrument_deps\n" );


    bbOut = deepCopyIRSBExceptStmts( bbIn );

#if PRINT_BB
    vex_printf("BBIN_START\n");
    ppIRSB( bbIn);
    vex_printf("BBIN_END\n");
#endif

    for( bbIn_idx = 0; bbIn_idx < bbIn->stmts_used; bbIn_idx++ ) {
       if( bbIn->stmts[bbIn_idx]->tag == Ist_IMark ) break;
       addStmtToIRSB( bbOut, bbIn->stmts[bbIn_idx] );
    }

    for( ; bbIn_idx < bbIn->stmts_used; bbIn_idx++ ) {
       stIn = bbIn->stmts[bbIn_idx];

#if !DO_NOT_INSTRUMENT       
       switch( stIn->tag ) {

         case Ist_IMark:
	     sr_index = -1;
             sr_code = SR_NONE;
             if( stack_modified ) { 
                emitSpChange( bbOut );
                stack_modified = 0;
             }
             guestIAddr = (Addr32) stIn->Ist.IMark.addr;
             guestILen  = stIn->Ist.IMark.len;
             loc_instr++;
#if PRECISE_ICOUNT
             emitIncrementGlobal( bbOut, &instructions, 1 );
#endif

#if RECORD_CF_EDGES
             emitDC_1( bbOut, "recordEdge", recordEdge, 
                       const32( (UInt) mk_line_info(guestIAddr) ) );
#endif

             currII = NULL;
             break;

         case Ist_Exit: 
             currII = mk_i_info( currII, guestIAddr, guestILen );
	     instrumentExit( bbOut, stIn->Ist.Exit.jk, currII, 
                             IRExpr_Const( stIn->Ist.Exit.dst ), loc_instr, stIn->Ist.Exit.guard );
#if !PRECISE_ICOUNT
             emitIncrementGlobal( bbOut, &instructions, loc_instr );
             loc_instr = 0;
#endif
             if( stack_modified ) { 
                emitSpChange( bbOut );
                stack_modified = 0;
             }
             break;

         case Ist_NoOp:
             break;

         case Ist_AbiHint:
             break;

         case Ist_Put:
             if( stIn->Ist.Put.offset == OFFSET_x86_ESP ) {
                stack_modified = 1;
             }
#if TRACE_REG_DEPS
             flags = sr_index != bbIn_idx ? 0 :
                     sr_code == SR_MOV_SP ? SI_MOV_SP : SI_SAVE_REST;
             currII = mk_i_info( currII, guestIAddr, guestILen );
             ref_size = sizeofIRType( typeOfIRExpr( bbIn->tyenv, stIn->Ist.Put.data ) );
             instrumentPut( bbOut, currII, stIn->Ist.Put.offset, ref_size, flags );
#endif
             break;
         case Ist_PutI:
#if TRACE_REG_DEPS
             // Similar to above, but needs to compute offset dynamically
             flags = sr_index==bbIn_idx ? SI_SAVE_REST : 0;
             currII = mk_i_info( currII, guestIAddr, guestILen );
             ref_size = sizeofIRType( typeOfIRExpr( bbIn->tyenv, stIn->Ist.PutI.data ) );
             instrumentPut( bbOut, currII, stIn, ref_size, flags );
#endif
             break;
         case Ist_WrTmp:
             // This is an assignment to a temp, so it should be instrumented
             // if the rhs is a Load, GET or GETI
             tmpExpr = stIn->Ist.WrTmp.data;
             switch( tmpExpr->tag ) {
               case Iex_Load:
                 currII = mk_i_info( currII, guestIAddr, guestILen );
                 ref_size = sizeofIRType( tmpExpr->Iex.Load.ty );
#if TRACE_REG_DEPS
                 flags = 0;
                 if( sr_index == -1 ) {
                    IRTemp t = stIn->Ist.WrTmp.tmp;
                    offset = sr_check( bbIn, bbIn_idx, t, SR_RESTORE, ref_size );
                    if( offset!=0 ) { 
                       sr_index = offset;
                       flags = SI_SAVE_REST;
                       sr_code = SR_RESTORE;
                    }
                 }
#endif
                 instrumentLoad( bbOut, currII, tmpExpr->Iex.Load.addr, ref_size, flags );
                 break;
#if TRACE_REG_DEPS
               case Iex_Get: // similar to above
                 currII = mk_i_info( currII, guestIAddr, guestILen );
                 ref_size = sizeofIRType( tmpExpr->Iex.Get.ty );
                 flags = 0;
                 // We never SAVE the stack pointer!
                 if( sr_index == -1 ) {
		    IRTemp t = stIn->Ist.WrTmp.tmp;
                    SRCode code = tmpExpr->Iex.Get.offset==OFFSET_x86_ESP ? SR_MOV_SP:SR_SAVE;
                    offset = sr_check( bbIn, bbIn_idx, t, code, ref_size );
                    if( offset != 0 ) { 
                      sr_index = offset;
                      sr_code = code;
                      flags = code==SR_MOV_SP ? SI_MOV_SP : SI_SAVE_REST;
                    }
                 }
                 instrumentGet( bbOut, currII, tmpExpr->Iex.Get.offset, ref_size, flags );
                 break;
               case Iex_GetI: // again similar, but needs to compute offset dynamically
                 currII = mk_i_info( currII, guestIAddr, guestILen );
                 ref_size = sizeofIRType( tmpExpr->Iex.GetI.descr->elemTy );
                 flags = 0;
                 if( sr_index == -1 ) {
                      IRTemp t = stIn->Ist.WrTmp.tmp;
                    offset = sr_check( bbIn, bbIn_idx, t, SR_SAVE, ref_size );
                    if( offset != 0 ) { 
                      sr_index = offset;
                      flags = SI_SAVE_REST;
                      sr_code = SR_SAVE;
                    }
                 }
                 instrumentGetI( bbOut, currII, tmpExpr, ref_size, flags );
                 break;
#endif
               default:
                 break;
             }
             break;
         case Ist_Store:
             // This is a store instruction, so it should be instrumented
#if TRACE_REG_DEPS
             flags = sr_index==bbIn_idx ? SI_SAVE_REST : 0;
#endif
             currII = mk_i_info( currII, guestIAddr, guestILen );
             // should handle save restore
             ref_size = sizeofIRType( typeOfIRExpr( bbIn->tyenv, stIn->Ist.Store.data ) );
             instrumentStore( bbOut, currII, stIn->Ist.Store.addr, ref_size, flags );
             break;
         case Ist_Dirty:
             // This should maybe be instrumented, but we'll leave it for later
             break;
         case Ist_MBE:
             break;
         default:
             VG_(tool_panic)("Unknown statement type!");
             break;

       }
#endif
       addStmtToIRSB( bbOut, stIn );
    }
#if !DO_NOT_INSTRUMENT
#if !PRECISE_ICOUNT
    emitIncrementGlobal( bbOut, &instructions, loc_instr );
#endif
    if( stack_modified ) { 
       emitSpChange( bbOut );
       stack_modified = 0;
    }
    currII = mk_i_info( currII, guestIAddr, guestILen );
    instrumentExit( bbOut, bbIn->jumpkind, currII, bbIn->next, loc_instr, NULL );
#endif
#if PRINT_BB
    vex_printf("BBOUT_START\n");
    ppIRSB( bbOut);
    vex_printf("BBOUT_END\n");
#endif
    return bbOut;
}

static IRSB* em_instrument(VgCallbackClosure* closure,
                           IRSB* bbIn, VexGuestLayout* layout,
			   VexGuestExtents * vge,
                           IRType gWordTy, IRType hWordTy)
{
   if( measure_span ) {
      return em_instrument_span( closure, bbIn, layout, vge, gWordTy, hWordTy );
   } else {
      return em_instrument_deps( closure, bbIn, layout, vge, gWordTy, hWordTy );
   }
}


/*********************************************
 * Initialization routines                   *
 *********************************************/

#define read_buf_size 4096

typedef struct {
   Int fd;
   Char *read_ptr, *end_ptr, buf[read_buf_size];
} ReadHandle;

static ReadHandle* openRH( const Char* name )
{
   SysRes sr = VG_(open)( name, VKI_O_RDONLY, 0 );
   int    fd = sr.res;

   ReadHandle *rh = (ReadHandle *) VG_(calloc)( 1, sizeof(ReadHandle) );

   check( fd > 0, "Cannot open file" );
   check( rh != NULL, "Cannot allocate read handle" );

   rh->fd = fd;
   rh->read_ptr = rh->buf;
   rh->end_ptr = rh->buf;

   return rh;
}

static void closeRH( ReadHandle *rh )
{
   VG_(close)( rh->fd );
   VG_(free)( rh );
}

static Bool eofRH( ReadHandle *rh )
{
   if( rh->read_ptr >= rh->end_ptr ) {
      rh->end_ptr = rh->buf + VG_(read)( rh->fd, rh->buf, read_buf_size );
      rh->read_ptr = rh->buf;
      if( rh->end_ptr == rh->buf ) return 1;
   }
   return 0;
}

static Char getcRH( ReadHandle *rh )
{
   if( eofRH( rh ) ) return 0;
   return *( (rh->read_ptr)++ );
}
   

// A word ends on a space or on the end of input. 
// The return value signifies length of word read with 0 indicating eof.
// The first character in the word returned is not a space.


static Int readWord( ReadHandle* rh, Char *o_buf, Int len )
{
   Char c = 0;
   Bool e;
   Int  n = len;

   while( !( e = eofRH( rh ) ) && VG_(isspace)( c = getcRH( rh ) ) ) ;

   if( !e ) {
      do {
         *( o_buf++ ) = c; 
         len--;
         if( eofRH( rh ) ) break;
         c = getcRH( rh );
      } while( ! VG_(isspace)( c ) && len > 1 );
   }
   *o_buf = 0;
   return n-len;
}

#define ADD_STATIC_DEP(li_src,li_tgt) (li_tgt)->deps = consLL( (li_src), (li_tgt)->deps )

static void readDeps( void )
{
   const int bs = 1000;
   Char filename[bs];
   Char fnname[bs];
   Char s_deptype[bs];
   Char s_tgt[bs];
   Char s_src[bs];
   ReadHandle *rh;
   

   rh = openRH( dep_file_name );
   
   // Format: "<file-name> <fn-name> <dependency type(s)> <target-line num> <source-line num>"
   while( 1 ) {
      int n1 = readWord( rh, filename, bs );
      int n2 = readWord( rh, fnname, bs );
      int n3 = readWord( rh, s_deptype, bs );
      int n4 = readWord( rh, s_tgt, bs );
      int n5 = readWord( rh, s_src, bs );
      LineInfo *li_tgt, *li_src;
      long n_tgt,n_src;
      int depType;

      if( !n1 || !n2 || !n3 || !n4 || !n5 ) break;

      n_tgt = VG_(atoll)( s_tgt );
      n_src = VG_(atoll)( s_src );
      depType = VG_(atoll16)( s_deptype );
      li_tgt = mk_line_info_l( filename, n_tgt, fnname );
      li_src = mk_line_info_l( filename, n_src, fnname );

      // Exclude hidden dependencies?
      if (track_hidden || !(depType & MASK_HIDDEN)) {
          if (sraw && (depType & MASK_RAW)) {
              // RAW dependency
              ADD_STATIC_DEP(li_src,li_tgt);
          } else if ((swar && (depType & MASK_WAR)) || (swaw && (depType & MASK_WAW))) {
              // WAR/WAW dependency

              // Exclude WAR/WAW dependencies on the stack?
              // Already excluded by DF_FALSE it seems......
              if (track_stack_name_deps || !(depType & MASK_STACK)) {
                 ADD_STATIC_DEP(li_src,li_tgt);
              }
          } else if (sctl && (depType & MASK_CTL)) {
              ADD_STATIC_DEP(li_src,li_tgt);
          }
      }
   }

   closeRH( rh );
}

static void em_post_clo_init_span( void )
{
   VG_(clo_vex_control).iropt_level = 0;
   VG_(clo_vex_control).iropt_unroll_thresh = 0;
   VG_(clo_vex_control).guest_chase_thresh = 0;

   questions = intern_string( "???" );
   dummy_line_info.file = questions;
   dummy_line_info.func = questions;

   VG_(message)(Vg_UserMsg, "Initializing span measurement");

   stack_base->call_header = NULL;

   readDeps( );

}

static void readHiddenFuncs( void)
{
   const int guess_size = 1000;
   Char fn[FN_LEN];
   ReadHandle *rh;

   numHiddenFuncs = 0;
   hiddenFuncs = VG_(malloc)(guess_size * sizeof(Char *));
   rh = openRH( hidden_func_file_name );
   
   while( 1 ) {
      int n = readWord( rh, fn, FN_LEN );

      if( !n ) break;

      hiddenFuncs[numHiddenFuncs] = VG_(malloc)(FN_LEN * sizeof(Char));
      VG_(strcpy)(hiddenFuncs[numHiddenFuncs], fn);
      numHiddenFuncs++;

   }

   closeRH( rh );

   VG_(realloc)(hiddenFuncs, numHiddenFuncs);
}

static void em_post_clo_init_deps(void)
{
   VG_(clo_vex_control).iropt_level = 0;
   VG_(clo_vex_control).iropt_unroll_thresh = 0;
   VG_(clo_vex_control).guest_chase_thresh = 0;

   VG_(message)(Vg_UserMsg, "Initalising dependency profiling");

   map = (MapFragment **) VG_(calloc)(FRAGS_IN_MAP, sizeof(MapFragment*));
   dirty_map = (unsigned int *) VG_(calloc)( DIRTY_WORDS, sizeof( unsigned int ) );
   trace_pile = (TraceRec *) VG_(calloc)( n_trace_recs, sizeof( TraceRec ) );
#if CRITPATH_ANALYSIS
   firstFrame = (FrameGraph *) VG_(calloc)( N_FRAMES, sizeof( FrameGraph ) );
#endif

   if( map==NULL || trace_pile==NULL || dirty_map==NULL ) {
       VG_(tool_panic)("Out of memory!");
   }
   first_new_tr = trace_pile;

#if CRITPATH_ANALYSIS
   if( firstFrame==NULL ) {
       VG_(tool_panic)("Out of memory!");
   }
   currFrame = firstFrame;
   currFrame->latestNodesTbl = initial_LatestNodeTable;
#endif

   opt.elim_stack_alias = 1;

   questions = intern_string( "???" );
   dummy_line_info.file = questions;
   dummy_line_info.func = questions;

   smarks[0].sp = (Addr32) 0xffffffff;
   smarks[0].tr = trace_pile+1;

   trace_pile[0].i_info = &dummy_instr_info;
   trace_pile[0].link = mkTaggedPtr2(stack_base, TPT_OPEN);

   trace_pile[1].i_info = &dummy_instr_info;
   trace_pile[1].link = mkTaggedPtr2(trace_pile, TPT_REG);

#if CRITPATH_ANALYSIS
   newInstr(trace_pile[0].i_info->line);
   trace_pile[0].inode = getINode();
   newNodeFrame(trace_pile[0].inode);

   newInstr(trace_pile[1].i_info->line);
   trace_pile[1].inode = getINode();
#endif

   last_trace_rec = trace_pile+1;
   stack_base->call_header = trace_pile;

   init_read_table( );

   sample_count = sample_freq;

   readHiddenFuncs( );

   // For efficiency purposes
   dyn_only = !(sctl || sraw || swar || swaw);
   sta_only = !(dctl || draw || dwar || dwaw);

   if (!dyn_only) {
     readDeps();
   }
}

static void em_post_clo_init(void)
{
   if( measure_span ) {
      em_post_clo_init_span( );
   } else {
      em_post_clo_init_deps( );
   }
}



/*****************************************************
 * Finalization routines                             *
 *****************************************************/

#if CRITPATH_ANALYSIS

static void finaliseCritPath(void) {
  long long int cpLength;

  while (currFrame > firstFrame) {
//    newInstr(dummy_instr_info.line);
    retNode();
  }
  cpLength = critPathNodes();//->cpLength;
  // To take account of the first 2 manually created TraceRecs
  VG_(message)(Vg_UserMsg, "No. of instructions is %lld", totalCost);
  VG_(message)(Vg_UserMsg, "Length of Critical path is %lld.", cpLength);
}

#endif

#if PRINT_RESULTS_TABLE
static void printResultTable(const Char * traceFileName)
{
  int      i,j;
   RTEntry *entry;
   SizeT results_buf_size = 100;
   RTEntry * result_array = VG_(malloc)(sizeof(RTEntry) * results_buf_size);
   SizeT num_results = 0;
   int fd = -1;
   LineInfo *line_info;

#if DUMP_TRACE_PILE
   dump_trace_pile( );
#endif

   tl_assert(result_array);
   for( i=0; i<RESULT_ENTRIES; i++ ) {
     // DPRINT1( "%d", i );
     for( line_info = line_table[i]; line_info != NULL; line_info = line_info->next )
       for( j=0; j<RT_ENTRIES_PER_LINE; j++ ) {
         // BONK( "|" );
         for( entry=line_info->entries[j]; entry != NULL; entry=entry->next ) {
           // BONK( "." );
           // DPRINT1( "%s\n", ( makeTitle( entry ) ) );
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
         }
       }
     // BONK( "\n" );
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
     fd = sres.res;
   }
   for (i = 0; i < num_results; ++i)
   {
     VG_(sprintf)( buf, "%s %d %d %d %d %d\n", 
		   makeTitle(& result_array[i]), result_array[i].n_raw,
		   result_array[i].n_war, result_array[i].n_waw);//,
//                   result_array[i].h_tr->i_info->i_addr, result_array[i].t_tr->i_info->i_addr );
//                   result_array[i].h_tr - trace_pile, result_array[i].t_tr - trace_pile);
     if (VG_(strcmp)(traceFileName, "-") != 0)
       VG_(write)( fd, buf, VG_(strlen)( buf ) );
     else
       VG_(printf)("%s", buf);
   }
   if (VG_(strcmp)(traceFileName, "-") != 0)
     VG_(close)( fd );
   VG_(free)( result_array );
}
#endif

typedef struct {
   LineInfo *from,*to;
} CFEdge;

static Int cf_edge_compare( CFEdge *a, CFEdge *b )
{
   int x;

   x = VG_(strcmp)( a->from->file, b->from->file );
   if( x != 0 ) return x;

   x = a->from->line - b->from->line;
   if( x != 0 ) return x;

   return a->to->line - b->to->line;
}

static void printCFG(const Char * cfgFileName)
{
   int      i,j;
   SizeT results_buf_size = 100;
   CFEdge * result_array = (CFEdge *) VG_(malloc)(sizeof(CFEdge) * results_buf_size);
   SizeT num_results = 0;
   int fd = -1;
   LineInfo *line_info;
   LineList *ll;

   // BONK( "printCFG\n" );

   tl_assert(result_array);
   for( i=0; i<RESULT_ENTRIES; i++ ) {

     for( line_info = line_table[i]; line_info != NULL; line_info = line_info->next )
       for( j=0; j<PRED_ENTRIES_PER_LINE; j++ ) {

         for( ll = line_info->pred[j]; ll != NULL; ll = ll->next ) {
           LineInfo *pred_line = ll->line;
           if ((VG_(strcmp)(pred_line->file, "???") != 0) &&
	       (VG_(strcmp)(pred_line->func, "???") != 0))
           {
             ++num_results;
             if (num_results >= results_buf_size)
             {
               results_buf_size *= 2;
               result_array = (CFEdge *) VG_(realloc)(result_array,
                                                      sizeof(CFEdge) * results_buf_size);
               tl_assert(result_array);
             }
             result_array[num_results - 1].from = pred_line;
             result_array[num_results - 1].to   = line_info;
           }
         }
       }

   }

   VG_(ssort)((void *) result_array, num_results, sizeof(CFEdge),
	      (Int (*)(void *, void *)) cf_edge_compare);

   if (VG_(strcmp)(cfgFileName, "-") != 0)
   {
      SysRes sres;
      sres = VG_(open)((Char *) cfgFileName, 
		       VKI_O_CREAT | VKI_O_TRUNC | VKI_O_WRONLY,
		       VKI_S_IRUSR | VKI_S_IWUSR | VKI_S_IRGRP);
     if( sres.isError ) {
       VG_(message)(Vg_UserMsg, "Edge file could not be opened");
       return;
     }
     fd = sres.res;
   }
   for (i = 0; i < num_results; ++i)
   {
     CFEdge *e = result_array + i;
     VG_(sprintf)( buf, "%s %d %d\n", e->from->file, e->from->line, e->to->line );
     if( fd != -1 )
       VG_(write)( fd, buf, VG_(strlen)( buf ) );
     else
       VG_(printf)("%s", buf);
   }
   if( fd != -1 )
     VG_(close)( fd );

}

static void printSPT( void )
{
   SPTEntry *ll;
   int        i;

   for( i=0; i < 1 << SEQ_PAR_TABLE_BITS; i++ ) {
     for( ll = seq_par_table[ i ]; ll != NULL; ll = ll->next ) {
       VG_(printf)( "%8s %4d %10llu %2llu\n", ll->line->file, ll->line->line,
                     ll->par,
                     100 * ll->par / instructions );
     }
   }
}

#define smdiv(x,y) (y==0 ? 0 : x/y)

static void em_fini_span(Int exitcode)
{
   TimeStamp span;
   VG_(message)(Vg_UserMsg, "Span measurement finished" );

   while( current_stack_frame > stack_base ) {
      current_stack_frame --;
      current_stack_frame->span += computeSpan( current_stack_frame + 1 );
   }
   span = computeSpan( stack_base );

   printSPT( );

   VG_(message)(Vg_UserMsg, "Instructions:        %12llu", instructions);
   VG_(message)(Vg_UserMsg, "Span:                %12llu", span);
   VG_(message)(Vg_UserMsg, "Average parallelism: %12llu", instructions /  span);
}

static void em_fini_deps(Int exitcode)
{
#if PRINT_RESULTS_TABLE
   VG_(message)(Vg_UserMsg, "Dependency trace has finished, storing in %s",
		trace_file_name);
   printResultTable( trace_file_name );
#endif

#if RECORD_CF_EDGES
   VG_(message)(Vg_UserMsg, "Control flow graph stored in %s",
		edge_file_name);
   printCFG( edge_file_name );
#endif

#if CRITPATH_ANALYSIS
   finaliseCritPath();
#endif

#if DO_PROFILE
   VG_(message)(Vg_UserMsg, "Max mem frags:    %10u (%10u bytes)", 
                            mem_table_frags, 
                            mem_table_frags*sizeof( MapFragment ) );
   VG_(message)(Vg_UserMsg, "Max stack frames: %10u (%10u bytes)", 
                            stack_frames,
                            stack_frames*sizeof( StackFrame ) );
   VG_(message)(Vg_UserMsg, "Max read list:    %10u (%10u bytes)", 
                            max_read_list_elements,
                            max_read_list_elements*sizeof( EventList ) );
   VG_(message)(Vg_UserMsg, "Instructions:   %12llu", instructions);
   VG_(message)(Vg_UserMsg, "# GC:   %lu", did_gc);

   VG_(message)(Vg_UserMsg, "inStack %llu %llu (%llu)", 
                            inStack_calls, inStack_iters, 
                            smdiv(10*inStack_iters, inStack_calls));
   VG_(message)(Vg_UserMsg, "forward %llu %llu (%llu)", 
                            forward_calls, forward_iters, 
                            smdiv(10*forward_iters, forward_calls));
   VG_(message)(Vg_UserMsg, "compactReads %llu %llu (%llu)", 
                            compactReads_items, compactReads_iters, 
                            smdiv(10*compactReads_iters, compactReads_items));
   VG_(message)(Vg_UserMsg, "allAndDirtyFrags %lu %lu (%lu)", 
                            all_frags, dirty_frags, 
                            smdiv(10*dirty_frags, all_frags));
   VG_(message)(Vg_UserMsg, "updResultEntry %llu nca:%llu(%llu) entry:%llu(%llu)", 
                            updResultEntry_calls, updResultEntry_nca, 
                            smdiv(10*updResultEntry_nca, updResultEntry_calls),
                            updResultEntry_entry,
                            smdiv(10*updResultEntry_entry, updResultEntry_calls));
#endif
}

static void em_fini(Int exitcode)
{
   if( measure_span ) {
      em_fini_span( exitcode );
   } else {
      em_fini_deps( exitcode );
}

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
