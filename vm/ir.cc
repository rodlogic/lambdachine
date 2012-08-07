#include "ir.hh"
#include "ir-inl.hh"
#include "assembler.hh"

#include <iostream>
#include <iomanip>

#include <string.h>
#include <limits.h>

_START_LAMBDACHINE_NAMESPACE

using namespace std;

uint8_t IR::mode_[k_MAX + 1] = {
#define IRMODE(name, flags, left, right) \
  (((IRM##left) | ((IRM##right) << 2)) | IRM_##flags),
  IRDEF(IRMODE)
#undef IRMODE
  0
};

#define STR(x) #x

const char *IR::name_[k_MAX + 1] = {
#define IRNAME(name, flags, left, right) STR(name),
  IRDEF(IRNAME)
#undef IRNAME
  "???"
};

static const char *tyname[] = {
#define IRTNAME(name, str, col) str,
  IRTDEF(IRTNAME)
#undef IRTNAME
};

enum {
  TC_NONE, TC_PRIM, TC_HEAP, TC_GREY,
  TC_MAX
};

static const uint8_t tycolor[] = {
#define IRTCOLOR(name, str, col) TC_##col,
  IRTDEF(IRTCOLOR)
#undef IRTCOLOR
};

static const char *tycolorcode[TC_MAX] = {
  "", COL_PURPLE, COL_RED, COL_GREY
};

void IR::printIRRef(std::ostream &out, IRRef ref) {
  if (ref < REF_BIAS) {
    out << 'K' << right << setw(3) << dec << setfill('0')
        << (int)(REF_BIAS - ref);
  } else {
    out << right << setw(4) << dec << setfill('0') << (int)(ref - REF_BIAS);
  }
}

static void printArg(ostream &out, uint8_t mode, uint16_t op, IR *ir, IRBuffer *buf) {
  switch ((IR::Mode)mode) {
  case IR::IRMnone:
    break;
  case IR::IRMref:
    out << ' ';
    IR::printIRRef(out, (IRRef)op);
    break;
  case IR::IRMlit:
    out << " #";
    out << setw(3) << setfill(' ') << left << (unsigned int)op;
    break;
  case IR::IRMcst:
    if (ir->opcode() == IR::kKINT) {
      int32_t i = ir->i32();
      char sign = (i < 0) ? '-' : '+';
      uint32_t k = (i < 0) ? -i : i;
      out << ' ' << COL_PURPLE << sign << k << COL_RESET;
    } else if (ir->opcode() == IR::kKWORD && buf != NULL &&
               (ir-1)->opcode() == IR::kKWORDHI) {
      uint64_t k = (uint64_t)ir->u32() | ((uint64_t)(ir-1)->u32() << 32);
      out << ' ' << COL_BLUE "0x" << hex << k << dec << COL_RESET;
    } else if (ir->opcode() == IR::kKBASEO) {
      out << " #" << left << ir->i32();
    } else {
      out << "<cst>";
    }
    break;
  default:
    break;
  }
}

static inline void print_reg(ostream &out, Reg r, IRType ty) {
  if (isReg(r)) {
    out << ' ' << setfill(' ') << setw(5) << left
        << IR::regName(r, ty);
  } else
    out << " -    ";
}

static inline void print_spill(ostream &out, uint8_t sp) {
  if (sp != 0) {
    out << '[' << setfill(' ') << setw(2) << right << (int)sp << ']';
  } else
    out << "    ";
}

void IR::debugPrint(ostream &out, IRRef self, IRBuffer *buf, bool regs) {
  IR::Opcode op = opcode();
  uint8_t ty = type();
  IR::printIRRef(out, self);
  if (regs) {
    print_reg(out, reg(), type());
    print_spill(out, spill());
  }
  out << "    "; // TODO: flags go here
  out << tycolorcode[tycolor[ty]];
  out << tyname[ty] << COL_RESET << ' ';
  out << setw(8) << setfill(' ') << left << name_[op];
  uint8_t mod = mode(op);
  printArg(out, mod & 3, op1(), this, buf);
  printArg(out, (mod >> 2) & 3, op2(), this, buf);
  out << endl;
}

void IRBuffer::debugPrint(ostream &out, int traceNo) {
  SnapNo snapno = 0;
  out << "---- TRACE " << right << setw(4) << setfill('0') << traceNo 
      << " IR -----------" << endl;
  for (IRRef ref = bufmin_; ref < bufmax_; ++ref) {
    IR *ins = ir(ref);
    if (ins->isGuard()) {
      Snapshot &sn = snap(snapno);
      LC_ASSERT(sn.ref() == ref);
      sn.debugPrint(out, snapmap(), snapno);
      ++snapno;
    }
    ins->debugPrint(out, ref, this, regsAllocated());
  }
}

IRBuffer::IRBuffer()
  : realbuffer_(NULL), flags_(), size_(1024), slots_(),
    snapmap_(), snaps_() {
  reset(NULL, NULL);
}

IRBuffer::~IRBuffer() {
  delete[] realbuffer_;
  realbuffer_ = NULL;
  buffer_ = NULL;
}

void IRBuffer::reset(Word *base, Word *top) {
  slots_.reset(base, top);
  if (realbuffer_) delete[] realbuffer_;
  realbuffer_ = new IR[size_];

  size_t nliterals = size_ / 4;

  bufstart_ = REF_BIAS - nliterals;
  bufend_ = bufstart_ + size_;

  buffer_ = biasBuffer(realbuffer_, nliterals);
  bufmin_ = REF_BIAS;
  bufmax_ = REF_BASE;

  flags_.set(kOptCSE);
  flags_.set(kOptFold);

  memset(chain_, 0, sizeof(chain_));
  emitRaw(IRT(IR::kBASE, IRT_PTR), 0, 0);
}

void IRBuffer::growTop() {
  cerr << "NYI: Growing IR buffer\n";
  exit(3);
}

void IRBuffer::growBottom() {
  cerr << "NYI: Growing IR buffer\n";
  exit(3);
}

TRef IRBuffer::emit() {
  IRRef ref = nextIns();
  IR *ir1 = ir(ref);
  IR::Opcode op = fold_.ins.opcode();
  
  ir1->setPrev(chain_[op]);
  chain_[op] = (IRRef1)ref;

  ir1->setOpcode(op);
  ir1->setOp1(fold_.ins.op1());
  ir1->setOp2(fold_.ins.op2());
  IR::Type t = fold_.ins.t();
  ir1->setT(t);

  if (t & IRT_GUARD)
    snapshot(ref, pc_);

  return TRef(ref, t);
}

TRef IRBuffer::literal(IRType ty, uint64_t lit) {
  IRRef ref;
  if (checki32(lit)) {
    int32_t k = (int32_t)lit;
    for (ref = chain_[IR::kKINT]; ref != 0; ref = buffer_[ref].prev()) {
      if (buffer_[ref].i32() == k && buffer_[ref].type() == ty)
        goto found;
    }
    ref = nextLit();  // Invalidates any IR*!
    IR *tir = ir(ref);
    tir->data_.i = k;
    tir->data_.t = (uint8_t)ty;
    tir->data_.o = IR::kKINT;
    tir->data_.prev = chain_[IR::kKINT];
    chain_[IR::kKINT] = (IRRef1)ref;
    return TRef(ref, ty);
  } else {
    // 64 bit constants are stored as a pair of KWORD and KWORDHI.
    // This has the same overhead as having a per-buffer array of 64
    // bit constants. We could reduce this overhead by having a pool
    // of 64 bit constants that is shared between all traces. That
    // requires a fast lookup mechanism, though, so I'm not sure it's
    // worth it.
    uint32_t klo = (uint32_t)lit;
    uint32_t khi = (uint32_t)(lit >> 32);
    for (ref = chain_[IR::kKWORD]; ref != 0; ref = buffer_[ref].prev()) {
      if (buffer_[ref].type() == ty &&
          buffer_[ref].data_.u == klo &&
          buffer_[ref-1].data_.u == khi)
        goto found;
    }
    IRRef reflo = nextLit();  // Invalidates any IR*!
    IRRef refhi = nextLit();
    LC_ASSERT(refhi == reflo - 1);
    IR *inslo = ir(reflo);
    inslo->data_.u = klo;
    inslo->data_.t = (uint8_t)ty;
    inslo->data_.o = IR::kKWORD;
    inslo->data_.prev = chain_[IR::kKWORD];
    IR *inshi = ir(refhi);
    inshi->data_.u = khi;
    inshi->data_.t = (uint8_t)ty;
    inshi->data_.o = IR::kKWORDHI;
    inshi->data_.prev = chain_[IR::kKWORDHI];
    chain_[IR::kKWORD] = (IRRef1)reflo;
    chain_[IR::kKWORDHI] = (IRRef1)refhi;
    return TRef(reflo, ty);
  }
 found:
  return TRef(ref, ty);
}

TRef IRBuffer::baseLiteral(Word *p) {
  int offset = slots_.baseOffset(p);
  IRRef ref;
  IR *tir;
  for (ref = chain_[IR::kKBASEO]; ref != 0; ref = buffer_[ref].prev()) {
    if (buffer_[ref].data_.i == offset)
      goto found;
  }
  ref = nextLit();
  tir = ir(ref);
  tir->data_.i = offset;
  tir->data_.t = IRT_PTR;
  tir->data_.o = IR::kKBASEO;
  tir->data_.prev = chain_[IR::kKBASEO];
  chain_[IR::kKBASEO] = (IRRef1)ref;
 found:
  return TRef(ref, IRT_PTR);
}

uint64_t IRBuffer::literalValue(IRRef ref) {
  IR *tir = ir(ref);
  if (tir->opcode() == IR::kKINT) {
    if (kOpIsSigned & (1 << (int)tir->type())) {
      return (int64_t)(int32_t)tir->i32();
    } else {
      return (uint64_t)(uint32_t)tir->i32();
    }
  } else if (tir->opcode() == IR::kKWORD) {
    return (uint64_t)tir->u32() | ((uint64_t)ir(ref-1)->u32() << 32);
  }
  LC_ASSERT(false);
  return 0;
}

TRef IRBuffer::optCSE() {
  if (flags_.get(kOptCSE)) {
    IRRef2 op12 =
      (IRRef2)fins()->op1() + ((IRRef2)fins()->op2() << 16);
    IR::Opcode op = fins()->opcode();
    if (true /* TODO: check if CSE is enabled */) {
      IRRef ref = chain_[op];
      IRRef lim = fins()->op1();
      if (fins()->op2() > lim) lim = fins()->op2();

      while (ref > lim) {
        if (ir(ref)->op12() == op12) {
          // Common subexpression found
          return TRef(ref, ir(ref)->t());
        }
        ref = ir(ref)->prev();
      }
    }
  }
  // Otherwise emit IR
  return emit();
}

void IRBuffer::snapshot(IRRef ref, void *pc) {
  Snapshot snap;
  slots_.snapshot(&snap, &snapmap_, ref, pc);
  snaps_.push_back(snap);
}

SnapNo IRBuffer::snapshot(void *pc) {
  snapshot(bufmax_, pc);
  return snaps_.size() - 1;
}

AbstractStack::AbstractStack() {
  slots_ = new TRef[kSlots];
  reset(NULL, NULL);
}

AbstractStack::~AbstractStack() {
  delete[] slots_;
  slots_ = NULL;
}

void AbstractStack::reset(Word *base, Word *top) {
  memset(slots_, 0, kSlots * sizeof(TRef));
  base_ = kInitialBase;
  LC_ASSERT(base <= top);
  realOrigBase_ = base;
  top_ = base_ + (top - base);
  LC_ASSERT(top_ < kSlots);
  low_ = base_;
  high_ = top_;
}

bool AbstractStack::frame(Word *base, Word *top) {
  int delta = base - realOrigBase_;
  if (delta < -(kInitialBase - 1)) return false;  // underflow
  base_ = kInitialBase + delta;
  top_ = base_ + (top - base);
  if (top_ >= kSlots) return false; // overflow
  return true;
}

void AbstractStack::snapshot(Snapshot *snap, SnapshotData *snapmap,
                             IRRef1 ref, void *pc) {
  unsigned int slot = low_;
  unsigned int entries = 0;
  unsigned int ofs = snapmap->index_;
  snapmap->data_.resize(ofs + high_ - low_);

  for ( ; slot <= high_; ++slot) {
    TRef tr = slots_[slot];
    if (tr.raw_ & TRef::kWritten) {
      int16_t slot_id = slot - kInitialBase;
      uint16_t ref = tr.ref();
      uint32_t data = ((uint32_t)slot_id << 16) | (uint32_t)ref;
      snapmap->data_.at(ofs) = data;
      ++ofs;
      ++entries;
    }
  }
  
  snap->ref_ = ref;
  snap->mapofs_ = snapmap->index_;
  snap->relbase_ = base_ - kInitialBase;
  snap->entries_ = entries;
  snap->framesize_ = top_ - base_;
  snap->exitCounter_ = 0;
  snap->pc_ = pc;

  snapmap->index_ = ofs;
}

void Snapshot::debugPrint(ostream &out, SnapshotData *snapmap, SnapNo snapno) {
  unsigned int ofs = mapofs_;
  int entries = entries_;

  out << "  SNAP #" << snapno << " [";

  if (entries > 0) {
    int slotid = snapmap->slotId(ofs);
    bool printslotid = true;
    bool nl = false;
    for ( ; entries > 0; ++slotid) {
      if (nl) out << "\n           ";
      if (printslotid)
        out << COL_BLUE << slotid << ':' << COL_RESET;
      if (snapmap->slotId(ofs) == slotid) {
        IR::printIRRef(out, snapmap->slotRef(ofs));
        ++ofs;
        --entries;
      } else {
        out << "----";
      }
      if (entries > 0) out << ' ';
      printslotid = (slotid % 4) == 3;
      nl = (slotid % 8) == 7;
    }
  }
  out << ']' << endl;
}

IRRef1 Snapshot::slot(int n, SnapshotData *snapmap) {
  // We use simple binary search.
  int lo = mapofs_;
  int hi = mapofs_ + entries_ - 1;
  LC_ASSERT(hi < INT_MAX/2);
  uint32_t data = 0;

  while (lo <= hi) {
    int mid = (lo + hi) >> 1;  // No overflow possible.
    data = snapmap->data_.at(mid);
    int slot = (int)(data >> 16);
    if (n > slot) {
      lo = mid + 1;
    } else if (n < slot) {
      hi = mid - 1;
    } else { 
      goto found;
    }
  }
  data = 0;  // Only executed if we didn't find anything.
 found:
  return (IRRef1)data;
}

SnapshotData::SnapshotData() : data_(), index_() { }

const char *IR::regName(uint8_t r, IRType ty) {
  switch (ty) {
  case IRT_I64: case IRT_U64:
  case IRT_CLOS: case IRT_INFO: case IRT_PC:
  case IRT_UNKNOWN: case IRT_PTR:
    LC_ASSERT(r < RID_MAX_GPR);
    return regNames64[r];
  case IRT_I32: case IRT_U32: case IRT_CHR:
    LC_ASSERT(r < RID_MAX_GPR);
    return regNames32[r];
  case IRT_F32: case IRT_F64:
    LC_ASSERT(RID_MIN_FPR <= r && r < RID_MAX_FPR);
    return fpRegNames[r - RID_MIN_FPR];
  default:
    exit(44);
  }
}

// Folding stuff is in ir_fold.cc

_END_LAMBDACHINE_NAMESPACE