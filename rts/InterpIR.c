#include "Jit.h"
#include "IR.h"
#include "Capability.h"
#include "Thread.h"
#include "PrintIR.h"
#include "MiscClosures.h"
#include "HeapInfo.h"
#include "StorageManager.h"

typedef void* Inst;

#define IR(ref)     (&F->ir[(ref)])

int
irEngine(Capability *cap, Fragment *F)
{
  static Inst disp[] = {
#define IRIMPL(name, f, o1, o2)  &&op_##name,
    IRDEF(IRIMPL)
#undef IRIMPL
    &&stop
  };

  Thread *T = cap->T;
  Word *base = T->base - 1;
  Word szins = F->nins - F->nk;
  Word vals_[szins];
  Word *vals = &vals_[F->nk] - REF_BIAS;
  IRIns *pc = F->ir + REF_FIRST;
  IRRef pcref = REF_FIRST;
  IRRef ref;
  IRIns *pcmax = F->ir + F->nins;
  IRIns *pcloop = F->nloop ? F->ir + F->nloop + 1 : pc;
  //int count = 100;

  DBG_PR("*** Executing trace.\n"
         "***   base  = %p\n"
         "***   pc    = %p\n"
         "***   pcmax = %p (%d)\n"
         "***   loop  = %p (%d)\n",
         base, pc, pcmax, pcmax - pc, pcloop, pcloop - pc);

  for (ref = F->nk; ref < REF_BIAS; ref++) {
    switch (IR(ref)->o) {
    case IR_KINT:   vals[ref] = (Word)IR(ref)->i; break;
    case IR_KBASEO: vals[ref] = (Word)(base + IR(ref)->i); break;
    case IR_KWORD:  vals[ref] = (Word)(cap->J.kwords[IR(ref)->u]); break;
    default:
      LC_ASSERT(0); break;
    }
    printf("%d, %" FMT_WordX "\n", ref - REF_BIAS, vals[ref]);
  }
  vals[REF_BASE] = (Word)base;

  goto *disp[pc->o];

# define DISPATCH_NEXT \
  if (irt_type(pc->t) != IRT_VOID && pc->o != IR_PHI) \
    if (irt_type(pc->t) == IRT_I32) \
      printf("         ===> %" FMT_Int "\n", vals[pcref]); \
    else \
      printf("         ===> 0x%" FMT_WordX "\n", vals[pcref]); \
  ++pc; ++pcref; \
  if (LC_UNLIKELY(pc >= pcmax)) { pc = pcloop; pcref = F->nloop + 1; } \
  printf("[%d] ", pcref - REF_BIAS); \
  printIR(&cap->J, *pc); \
  goto *disp[pc->o]

 op_NOP:
 op_FRAME:
 op_RET:
 op_LOOP:
  DISPATCH_NEXT;

 op_PHI:
  vals[pc->op1] = vals[pc->op2];
  DISPATCH_NEXT;

 op_LT:
  if (!((WordInt)vals[pc->op1] < (WordInt)vals[pc->op2]))
    goto guard_failed;
  DISPATCH_NEXT;

 op_GE:
  if (!((WordInt)vals[pc->op1] >= (WordInt)vals[pc->op2]))
    goto guard_failed;
  DISPATCH_NEXT;

 op_LE:
  if (!((WordInt)vals[pc->op1] <= (WordInt)vals[pc->op2]))
    goto guard_failed;
  DISPATCH_NEXT;

 op_GT:
  if (!((WordInt)vals[pc->op1] > (WordInt)vals[pc->op2]))
    goto guard_failed;
  DISPATCH_NEXT;

 op_EQ:
  if (!((WordInt)vals[pc->op1] == (WordInt)vals[pc->op2])) {
    goto guard_failed;
  }
  DISPATCH_NEXT;

 op_NE:
  if (!((WordInt)vals[pc->op1] != (WordInt)vals[pc->op2]))
    goto guard_failed;
  DISPATCH_NEXT;

 op_ADD:
  vals[pcref] = vals[pc->op1] + vals[pc->op2];
  DISPATCH_NEXT;

 op_SUB:
  vals[pcref] = vals[pc->op1] - vals[pc->op2];
  DISPATCH_NEXT;

 op_MUL:
  vals[pcref] = vals[pc->op1] * vals[pc->op2];
  DISPATCH_NEXT;

 op_FREF:
  vals[pcref] = (Word)(((Closure*)vals[pc->op1])->payload + (pc->op2 - 1));
  DISPATCH_NEXT;

 op_FLOAD:
  vals[pcref] = *((Word*)vals[pc->op1]);
  DISPATCH_NEXT;

 op_SLOAD:
  vals[pcref] = base[pc->op1];
  DISPATCH_NEXT;

 op_ILOAD:
  vals[pcref] = (Word)getInfo(vals[pc->op1]);
  DISPATCH_NEXT;

 op_NEW:
  if (F->heap[pc->op2].loop & 1) {
    // TODO: do actual allocation on trace
    LC_ASSERT(0);
  } else {
    vals[pcref] = 0;  // to trigger an error if accessed
  }
  DISPATCH_NEXT;

 op_UPDATE:
  {
    Closure *oldnode = (Closure *)vals[pc->op1];
    Closure *newnode = (Closure *)base[pc->op2];
    setInfo(oldnode, (InfoTable*)&stg_IND_info);
    oldnode->payload[0] = (Word)newnode;
    DISPATCH_NEXT;
  }

 op_RLOAD:
 op_FSTORE:
 op_RENAME:


 op_BNOT: op_BAND: op_BOR: op_BXOR:
 op_BSHL: op_BSHR: op_BSAR:
 op_BROL: op_BROR:
 op_DIV:

  // These should never be executed.
 op_BASE:
 op_KINT:
 op_KWORD:
 op_KBASEO:
  LC_ASSERT(0);

 guard_failed:
  printf("Exiting at %d\n", pcref - REF_BIAS);

  {
    int i;
    SnapShot *snap = 0;
    SnapEntry *se;
    for (i = 0; i < F->nsnap; i++) {
      if (F->snap[i].ref == pcref) {
        snap = &F->snap[i];
        break;
      }
    }
    LC_ASSERT(snap != 0);
    snap->count++;
    se = F->snapmap + snap->mapofs;
    DBG_PR("Snap entries: %d, slots = %d\n",
           snap->nent, snap->nslots);
    for (i = 0; i < snap->nent; i++, se++) {
      BCReg s = snap_slot(*se);
      IRRef r = snap_ref(*se);
      IRIns *ir = IR(r);
      DBG_PR("base[%d] = ", s - 1);

      if (irref_islit(r) || ir->o != IR_NEW)
        base[s] = vals[r];
      else { // ir->o == IR_NEW
        if (!(F->heap[ir->op2].loop & 1)) {
          // Need to allocate closure now.
          HeapInfo *hp = &F->heap[ir->op2];
          Closure *cl = allocClosure(wordsof(ClosureHeader) + hp->nfields);
          int j;
          DBG_PR("(alloc[%lu])", wordsof(ClosureHeader) + hp->nfields);
          setInfo(cl, (InfoTable*)vals[ir->op1]);
          for (j = 0; j < hp->nfields; j++) {
            cl->payload[j] = vals[getHeapInfoField(&cap->J, hp, j)];
          }
          base[s] = (Word)cl;
        } else 
          // Closure has been allocated on trace
          base[s] = vals[r];
      }
      printSlot(base + s); printf("\n");
      //DBG_PR("0x%" FMT_WordX "\n", base[s]);
    }
    DBG_PR("Base slot: %d\n", se[1]);
    //    se[1] = 
    T->base = base + se[1];
    T->top = base + snap->nslots;
    printFrame(T->base, T->top);
    return 0;
  }

 stop:
  return 1;
}

#undef IR
