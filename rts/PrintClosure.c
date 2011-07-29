#include <stdio.h>
#include "InfoTables.h"
#include "PrintClosure.h"
#include "Bytecode.h"
#include "StorageManager.h"

void printInlineBitmap(const BCIns *p0);

void
printClosure_(FILE *f, Closure* cl, int nl)
{
  const InfoTable *info = getInfo(cl);

  if (info == NULL) {
    fprintf(f, "???\n");
    return;
  }

  if (info->type == IND) {
    cl = (Closure*)cl->payload[0];
    info = getInfo(cl);
    fprintf(f, "IND -> ");
  }

  switch (info->type) {
  case CONSTR:
    fprintf(f, "%s ", cast(ConInfoTable*,info)->name);
    break;
  case FUN:
    fprintf(f, "%s ", cast(FuncInfoTable*,info)->name);
    break;
  case THUNK:
  case CAF:
    fprintf(f, "%s ", cast(ThunkInfoTable*,info)->name);
    break;
  }

  int n, p = 0;
  u4 bitmap = info->layout.bitmap;
  for (n = info->size; n > 0; p++, n--, bitmap = bitmap >> 1) {
    if (bitmap & 1)
      fprintf(f, "%p ", (Word*)cl->payload[p]);
    else
      fprintf(f, "%" FMT_Int " ", cl->payload[p]);
  }

  /* For when are we going to use ptr/nptr layout
  for (n = info->layout.payload.ptrs; n > 0; p++, n--)
    printf("%0" FMT_WordLen FMT_WordX " ", cl->payload[p]);
  for (n = info->layout.payload.nptrs; n > 0; p++, n--)
    printf("%" FMT_Int " ", cl->payload[p]);
  */
  if (nl) 
    fprintf(f, "\n");
  
}

u4 printInstruction_aux(const BCIns *ins /*in*/, int oneline);

u4
printInstruction(const BCIns *ins) {
  return printInstruction_aux(ins, 0);
}

u4
printInstructionOneLine(const BCIns *ins) {
  return printInstruction_aux(ins, 1);
}

u4
printInstruction_aux(const BCIns *ins /*in*/, int oneline)
{
  const BCIns *ins0 = ins;
  u4 j;
  const BCIns i = *ins;
  const char *name = ins_name[bc_op(i)];

  printf("%p: ", ins);
  ++ ins;

  switch(ins_format[bc_op(i)]) {
  case IFM_R:
    printf("%s\tr%d\n", name, bc_a(i)); break;
  case IFM_RR:
    printf("%s\tr%d, r%d\n", name, bc_a(i), bc_d(i)); break;
  case IFM_RRR:
    printf("%s\tr%d, r%d, r%d\n", name, bc_a(i), bc_b(i), bc_c(i));
    break;
  case IFM_RN:
    printf("%s\tr%d, %d\n", name, bc_a(i), bc_d(i)); break;
  case IFM_RRN:
    printf("%s\tr%d, r%d, %d\n", name, bc_a(i), bc_b(i), bc_c(i));
    break;
  case IFM_RS:
    printf("%s\tr%d, %d\n", name, bc_a(i), bc_sd(i)); break;
  case IFM_J:
    printf("%s\t%p\n", name, ins + bc_j(i)); break;
  case IFM_RRJ:
    printf("%s\tr%d, r%d, %p\n", name, bc_a(i), bc_d(i),
           ins + 1 + bc_j(*ins));
    ins++;
    break;
  case IFM____:
    switch (bc_op(i)) {
    case BC_EVAL:
      { printf("EVAL\tr%d", bc_a(i));
        printInlineBitmap(ins);
        ins++;
      }
      break;
    case BC_CASE:
      { u2 *tgt = (u2*)ins;  u4 ncases = bc_d(i);
        ins += (ncases + 1) / 2;
        printf("CASE\tr%d\n", bc_a(i));
        if (!oneline) {
          for (j = 0; j < ncases; j++, tgt++) {
            printf("         %d: %p\n", j + 1, ins + bc_j_from_d(*tgt));
          }
        }
      }
      break;
    case BC_CASE_S:
      printf("CASE_S\tr%d ...TODO...\n", bc_a(i));
      ins += bc_d(i);
      break;
    case BC_ALLOC1:
      printf("ALLOC1\tr%d, r%d, r%d", bc_a(i), bc_b(i), bc_c(i));
      printInlineBitmap(ins);
      ins += 1;
      break;

    case BC_ALLOC:
      {
        u1 *arg = (u1*)ins; ins += 1 + BC_ROUND(bc_c(i));
        printf("ALLOC\tr%d, r%d", bc_a(i), bc_b(i));
        for (j = 0; j < bc_c(i); j++, arg++)
          printf(", r%d", *arg);
        printInlineBitmap(ins - 1);
      }
      break;
    case BC_ALLOCAP:
      {
        u1 *arg = (u1*)ins; ins += 1 + BC_ROUND(bc_c(i) + 1);
        printf("ALLOCAP\tr%d", bc_a(i));
        u1 ptrmask = bc_b(i);
        printf(", r%d", *arg++);
        for (j = 1; j < bc_c(i); j++, arg++) {
          printf(", r%d%c", *arg, ptrmask & 1 ? '*' : ' ');
          ptrmask >>= 1;
        }
        printInlineBitmap(ins - 1);
      }
      break;
    case BC_CALL:
      { u1 *arg = (u1*)ins; ins += BC_ROUND(bc_c(i)) + 1;
        printf("%s\tr%d", name, bc_a(i));
        char comma = '(';
        for (j = 0; j < bc_c(i); j++, arg++) {
          printf("%cr%d", comma, *arg);
          comma = ',';
        }
        printf(") [%x]", bc_b(i));
        printInlineBitmap(ins - 1);
      }
      break;
    case BC_CALLT:
      { 
        int j;
        u1 bitmask = bc_b(i);
        printf("CALLT r%d", bc_a(i));
        for (j = 0; j < bc_c(i); j++) {
          printf("%cr%d%c", j == 0 ? '(' : ',', j, bitmask & 1 ? '*' : ' ');
          bitmask >>= 1;
        }
        printf(")\n");
      }
      break;
    default:
      printf("%s ...TODO...\n", name);
    }
    break;
  default:
    fprintf(stderr, "FATAL: Unknown intruction format: %d\n",
            ins_format[bc_op(i)]);
  }

  return (u4)(ins - ins0);
}

const u2 *
printInlineBitmap_(const u2 *p)
{
  u2 bitmap;
  int min = 0;
  int i;
  // Live pointers
  printf("(%p) { ", p);
  do {
    bitmap = *p;
    for (i = 0; i < 15 && bitmap != 0; i++) {
      if (bitmap & 1)
        printf("r%d ", min + i);
      bitmap = bitmap >> 1;
    }
    ++p;
  } while (bitmap != 0);
  printf("}");
  return p;
}

void
printInlineBitmap(const BCIns *p0)
{
  u4 offset = (u4)(*p0);
  if (offset != 0) {
    const u2 *p = (const u2*)((u1*)p0 + offset);
    putchar('\t');
    p = printInlineBitmap_(p); // pointers
    printf(" / ");
    printInlineBitmap_(p);   // Live-out variables
    putchar('\n');
  }
}

void
printPointerBitmap(InfoTable *info)
{
  int i; u4 bitmap;
  i = info->size;
  if (i <= 32) {
    bitmap = info->layout.bitmap;
    printf("  pointers: (%d) ", (int)i);
    while (i > 0) {
      if (bitmap & 1) putchar('*'); else putchar('-');
      i--;
      bitmap = bitmap >> 1;
    }
  } else {
    printf("  pointers: ?");
  }
  putchar('\n');
}

void
printPtrNPtr(InfoTable *info)
{
  printf("  ptrs/nptrs: %d/%d\n",
         info->layout.payload.ptrs, info->layout.payload.nptrs);
}

void
printInfoTable(InfoTable* info0)
{
  switch (info0->type) {
  case CONSTR:
    {
      ConInfoTable* info = (ConInfoTable*)info0;
      printf("Constructor: %s, (%p)\n", info->name, info);
      printf("  tag: %d\n", info->i.tagOrBitmap);
      printPointerBitmap(info0);
    }
    break;
  case FUN:
    {
      FuncInfoTable *info = (FuncInfoTable*)info0;
      printf("Function: %s (%p)\n", info->name, info);
      printPointerBitmap(info0);
      printCode(&info->code);
    }
    break;
  case CAF:
  case THUNK:
    {
      ThunkInfoTable *info = (ThunkInfoTable*)info0;
      printf("%s: %s (%p)\n",
             info0->type == THUNK ? "Thunk" : "CAF",
             info->name, info);
      printPointerBitmap(info0);
      printCode(&info->code);
    }
    break;
  default:
    printf("Unknown info table\n");
  }
  printf("\n");
}

void
printCode(LcCode *code)
{
  u4 i; u4 nc = 0; BCIns *c = code->code;
  if (code->arity > 0) {
    printf("  arity: %d, ptrs: ", code->arity);
    // First bitmap is the function pointer map
    printInlineBitmap_((const u2 *)&code->code[code->sizecode]);
    putchar('\n');
  }
  printf("  frame: %d\n", code->framesize);
  printf("  literals:\n");
  for (i = 0; i < code->sizelits; i++) {
    printf("   %3d: ", i);
    switch (code->littypes[i]) {
    case LIT_INT:
      printf("%" FMT_Int " (i)", (WordInt)code->lits[i]);
      break;
    case LIT_WORD:
      printf("%" FMT_Word " (w)", (Word)code->lits[i]);
      break;
    case LIT_FLOAT:
      printf("%f / %" FMT_WordX, *((float*)&code->lits[i]), code->lits[i]);
      break;
    case LIT_CHAR:
      { Word c = code->lits[i];
	if (c < 256)
	  printf("'%c'", (char)c);
	else
	  printf("u%xd", (u4)c);
      }
      break;
    case LIT_STRING:
      printf("\"%s\"", (char*)code->lits[i]);
      break;
    case LIT_CLOSURE:
      printf("clos %" FMT_WordX " (%s)", code->lits[i],
             getFInfo(code->lits[i])->name);
      break;
    case LIT_INFO:
      printf("info %" FMT_WordX " (%s)", code->lits[i],
             cast(FuncInfoTable*,code->lits[i])->name);
      break;
    default:
      printf("???");
    }
    printf("\n");
  }
  printf("  code:\n");
  while (nc < code->sizecode) {
    i = printInstruction(c);
    c += i;
    nc += i;
  }
}
