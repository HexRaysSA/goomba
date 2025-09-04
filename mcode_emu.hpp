/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *      This file implements a simple microcode emulator class
 *
 */

#pragma once
#include <hexrays.hpp>

//-------------------------------------------------------------------------
// truncate v to w bytes
inline uint64 trunc(uint64 v, int w)
{
  QASSERT(30660, w == 1 || w == 2 || w == 4 || w == 8);
  return v & make_mask<uint64>(w * 8);
}

//-------------------------------------------------------------------------
struct mcode_val_t
{
  uint64 val;
  int size; // in bytes

  //-------------------------------------------------------------------------
  void check_size_equal(const mcode_val_t &o) const
  {
    QASSERT(30661, size == o.size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t(uint64 v, int s) : val(trunc(v, s)), size(s) {}

  //-------------------------------------------------------------------------
  int64 signed_val() const
  {
    return extend_sign(val, size, true);
  }

  //-------------------------------------------------------------------------
  mcode_val_t sext(int target_sz) const
  {
    QASSERT(30662, target_sz >= size);
    return mcode_val_t(signed_val(), target_sz);
  }

  //-------------------------------------------------------------------------
  mcode_val_t zext(int target_sz) const
  {
    QASSERT(30663, target_sz >= size);
    return mcode_val_t(val, target_sz);
  }

  //-------------------------------------------------------------------------
  mcode_val_t low(int target_sz) const
  {
    QASSERT(30664, target_sz <= size);
    return mcode_val_t(val, target_sz);
  }

  //-------------------------------------------------------------------------
  mcode_val_t high(int target_sz) const
  {
    QASSERT(30665, target_sz <= size);
    int bytes_to_remove = size - target_sz;
    return mcode_val_t(right_ushift<uint64>(val, 8 * bytes_to_remove), target_sz);
  }

  //-------------------------------------------------------------------------
  bool operator==(const mcode_val_t &o) const
  {
    return size == o.size && val == o.val;
  }

  //-------------------------------------------------------------------------
  bool operator!=(const mcode_val_t &o) const
  {
    return !(*this == o);
  }

  //-------------------------------------------------------------------------
  bool operator<(const mcode_val_t &o) const
  {
    QASSERT(30702, size == o.size);
    return val < o.val;
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator+(const mcode_val_t &o) const
  {
    check_size_equal(o);
    return mcode_val_t(val + o.val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator-(const mcode_val_t &o) const
  {
    check_size_equal(o);
    return mcode_val_t(val - o.val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator*(const mcode_val_t &o) const
  {
    check_size_equal(o);
    return mcode_val_t(val * o.val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator/(const mcode_val_t &o) const
  {
    check_size_equal(o);
    if ( o.val == 0 )
      throw "division by zero occurred when emulating instruction";
    return mcode_val_t(val / o.val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t sdiv(const mcode_val_t &o) const
  {
    check_size_equal(o);
    if ( o.val == 0 )
      throw "division by zero occurred when emulating instruction";
    int64 res;
    uint64 l = val;
    uint64 r = o.val;
    switch ( size )
    {
      case 1: res = int8(l)  / int8(r); break;
      case 2: res = int16(l) / int16(r); break;
      case 4: res = int32(l) / int32(r); break;
      case 8: res = int64(l) / int64(r); break;
      default: INTERR(30666);
    }

    return mcode_val_t(res, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator%(const mcode_val_t &o) const
  {
    check_size_equal(o);
    if ( o.val == 0 )
      throw "division by zero occurred when emulating instruction";
    return mcode_val_t(val % o.val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t smod(const mcode_val_t &o) const
  {
    check_size_equal(o);
    if ( o.val == 0 )
      throw "division by zero occurred when emulating instruction";
    int64 res = -1;
    uint64 l = val;
    uint64 r = o.val;
    switch ( size )
    {
      case 1: res = int8(l)  % int8(r); break;
      case 2: res = int16(l) % int16(r); break;
      case 4: res = int32(l) % int32(r); break;
      case 8: res = int64(l) % int64(r); break;
      default: QASSERT(30667, false);
    }

    return mcode_val_t(res, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator<<(const mcode_val_t &o) const
  {
    return mcode_val_t(left_shift<uint64>(val, o.val), size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator>>(const mcode_val_t &o) const
  {
    return mcode_val_t(right_ushift<uint64>(val, o.val), size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t sar(const mcode_val_t &o) const
  {
    return mcode_val_t(right_sshift<int64>(signed_val(), o.val), size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator|(const mcode_val_t &o) const
  {
    check_size_equal(o);
    return mcode_val_t(val | o.val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator&(const mcode_val_t &o) const
  {
    check_size_equal(o);
    return mcode_val_t(val & o.val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator^(const mcode_val_t &o) const
  {
    check_size_equal(o);
    return mcode_val_t(val ^ o.val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator-() const
  {
    return mcode_val_t(0-val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator!() const
  {
    return mcode_val_t(!val, size);
  }

  //-------------------------------------------------------------------------
  mcode_val_t operator~() const
  {
    return mcode_val_t(~val, size);
  }
};

//-------------------------------------------------------------------------
class mcode_emulator_t
{
public:
  // base classes with virtual functions should have a virtual dtr
  virtual ~mcode_emulator_t() {}
  // returns the value assigned to a register, stack, global, or local variable
  virtual mcode_val_t get_var_val(const mop_t &mop) = 0;

  //-------------------------------------------------------------------------
  mcode_val_t mop_value(const mop_t &mop)
  {
    if ( mop.size > 8 )
      throw "too big mop size in mcode emulator";
    switch ( mop.t )
    {
      case mop_n:
        return mcode_val_t(mop.nnn->value, mop.size);
      case mop_d:
        return minsn_value(*mop.d);
      case mop_r: // register
      case mop_S: // stack variable
      case mop_v: // global variable
      case mop_l:
        return get_var_val(mop);
      default:
        throw "unhandled mop type in mcode emulator";
    }
  }

  //-------------------------------------------------------------------------
  mcode_val_t minsn_value(const minsn_t &insn)
  {
    if ( insn.is_fpinsn() )
    {
      msg("Emulator does not support floating point\n");
      throw "Emulator does not support floating point";
    }
    switch ( insn.opcode )
    {
      case m_ldc:
      case m_mov:
        return mop_value(insn.l);
      case m_neg:
        return -mop_value(insn.l);
      case m_lnot:
        return !mop_value(insn.l);
      case m_bnot:
        return ~mop_value(insn.l);
      case m_xds:
        return mop_value(insn.l).sext(insn.d.size);
      case m_xdu:
        return mop_value(insn.l).zext(insn.d.size);
      case m_low:
        return mop_value(insn.l).low(insn.d.size);
      case m_high:
        return mop_value(insn.l).high(insn.d.size);
      case m_add:
        return mop_value(insn.l) + mop_value(insn.r);
      case m_sub:
        return mop_value(insn.l) - mop_value(insn.r);
      case m_mul:
        return mop_value(insn.l) * mop_value(insn.r);
      case m_udiv:
        return mop_value(insn.l) / mop_value(insn.r);
      case m_sdiv:
        return mop_value(insn.l).sdiv(mop_value(insn.r));
      case m_umod:
        return mop_value(insn.l) & mop_value(insn.r);
      case m_smod:
        return mop_value(insn.l).smod(mop_value(insn.r));
      case m_or:
        return mop_value(insn.l) | mop_value(insn.r);
      case m_and:
        return mop_value(insn.l) & mop_value(insn.r);
      case m_xor:
        return mop_value(insn.l) ^ mop_value(insn.r);
      case m_shl:
        return mop_value(insn.l) << mop_value(insn.r);
      case m_shr:
        return mop_value(insn.l) >> mop_value(insn.r);
      case m_sar:
        return mop_value(insn.l).sar(mop_value(insn.r));
      case m_sets:
        return mcode_val_t(mop_value(insn.l).signed_val() < 0, insn.d.size);
      case m_setnz:
        return mcode_val_t(mop_value(insn.l) != mop_value(insn.r), insn.d.size);
      case m_setz:
        return mcode_val_t(mop_value(insn.l) == mop_value(insn.r), insn.d.size);
      case m_setae:
        return mcode_val_t(mop_value(insn.l).val >= mop_value(insn.r).val, insn.d.size);
      case m_setb:
        return mcode_val_t(mop_value(insn.l).val < mop_value(insn.r).val, insn.d.size);
      case m_seta:
        return mcode_val_t(mop_value(insn.l).val > mop_value(insn.r).val, insn.d.size);
      case m_setbe:
        return mcode_val_t(mop_value(insn.l).val <= mop_value(insn.r).val, insn.d.size);
      case m_setg:
        return mcode_val_t(mop_value(insn.l).signed_val() > mop_value(insn.r).signed_val(), insn.d.size);
      case m_setge:
        return mcode_val_t(mop_value(insn.l).signed_val() >= mop_value(insn.r).signed_val(), insn.d.size);
      case m_setl:
        return mcode_val_t(mop_value(insn.l).signed_val() < mop_value(insn.r).signed_val(), insn.d.size);
      case m_setle:
        return mcode_val_t(mop_value(insn.l).signed_val() <= mop_value(insn.r).signed_val(), insn.d.size);
      default:
        msg("Unhandled opcode in emulator %d\n", insn.opcode);
        throw "Unhandled opcode";
    }
  }
};
