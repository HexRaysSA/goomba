/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include <hexrays.hpp>

//#define LDEB      // enable debug print

//--------------------------------------------------------------------------
inline AS_PRINTF(1, 2) void dmsg(const char *format, ...)
{
#ifdef LDEB
  va_list va;
  va_start(va, format);
  vmsg(format, va);
  va_end(va);
#else
  qnotused(format);
#endif
}

//--------------------------------------------------------------------------
class nonlin_expr_t : public candidate_expr_t
{
  // constant values to be used when building new candidates
  mop_t one;
  mop_t two;
  mop_t minus_one;

  minsn_t *cur_mba;       // copy of the instruction to normalize
  ea_t cur_ea = BADADDR;  // during recursion: address of the current insn
  bool ok = false;

  // Terminology of MBA
  // ==================
  //
  //   3 * x * (x&y) + 2 * y * z + 1
  //       -   -----       -   -        factor
  //       ---------       -----        product
  //   -               -           -    coefficient
  //   -------------   ---------   -    term

  // A factor could be a variable or an AND expression.
  // A variable could be x or -x
  // An AND expression could include -x, x+y, or x-y
  class factor_t
  {
    enum factor_type_t { VAR, AND_EXPR };

    factor_type_t t;

  public:
    std::set<mop_t> ops;
    mop_t op;

    factor_t(const mop_t &arg1) : t(VAR), op(arg1) {}
    factor_t(const std::set<mop_t> &arg1, mop_t arg2) : t(AND_EXPR), ops(arg1), op(arg2) {}

    bool is_var() { return t == VAR; }
    bool is_and_expr() { return t == AND_EXPR; }

    //--------------------------------------------------------------------------
    // override < and == operator for using std::map
    bool operator<(const factor_t &right) const
    {
      if ( t == VAR && right.t == VAR )
      {
        return op < right.op;
      }
      else if ( t == AND_EXPR && right.t == AND_EXPR )
      {
        return ops < right.ops;
      }
      else
      { // either t == VAR && right.t == AND_EXPR  or  t == AND_EXPR && right.t == VAR
        return t == VAR;
      }
    }

    //--------------------------------------------------------------------------
    bool operator==(const factor_t &right) const
    {
      if ( t == VAR && right.t == VAR )
      {
        return op == right.op;
      }
      else if ( t == AND_EXPR && right.t == AND_EXPR )
      {
        return ops == right.ops;
      }
      else
      {
        return false;
      }
    }

    //--------------------------------------------------------------------------
    const char *dstr() const
    {
      if ( t == VAR )
        return op.dstr();

      if ( t == AND_EXPR )
      {
        qstring s("AND(");
        for ( const mop_t &elem : ops )
        {
          s = s + elem.dstr() + ",";
        }
        s += ")";
        return s.c_str();
      }
      INTERR(0);
    }

    //--------------------------------------------------------------------------
    mop_t to_mop() const
    {
      return op;
    }
  };

  //--------------------------------------------------------------------------
  // The product of factors is represented by a frequency map, where the number of each factor
  // are counted. It works because multiplication is commutative and associative, so the order of
  // the factors doesn't matter. For example:
  // x*y*(x&y)*y*x*y*z can be represented as: { (x, 2), (y, 3), (z, 1), (x&y, 1) }
  typedef std::map<factor_t, int> product_t;

  //--------------------------------------------------------------------------
  // A term is the multiplication of a signed coefficient and a prodcut
  class term_t
  {
  public:
    int coeff;
    product_t prod;

    //--------------------------------------------------------------------------
    term_t(minsn_t *ins, int is_add) : term_t(insn_to_mop(ins), is_add)
    {
    }
    //--------------------------------------------------------------------------
    term_t(const mop_t &op, int is_add)
    {
      if ( op.is_constant() )
      {
        coeff = is_add * op.signed_value();       // is_add is 1 for + and -1 for -
        return;
      }
      if ( is_product(op) )
      {
        coeff = is_add;
        build_product(&prod, op);
        return;
      }
      if ( op.is_insn(m_mul) )
      {
        minsn_t *ins = op.d;
        if ( ins->l.is_constant() && is_product(ins->r) )          // n * product
        {
          coeff = is_add * ins->l.signed_value();
          build_product(&prod, ins->r);
        }
        else if ( ins->r.is_constant() && is_product(ins->l) )     // product * n
        {
          coeff = is_add * ins->r.signed_value();
          build_product(&prod, ins->l);
        }
        return;
      }
      INTERR(0);
    }

    //--------------------------------------------------------------------------
    mop_t to_mop(const nonlin_expr_t *nlex) const
    {
      mop_t res;
      res.make_number(coeff, nlex->one.size);

      for ( auto &p : prod )
      {
        mop_t f = p.first.to_mop();

        for ( int i = 0; i < p.second; i++ )
        {
          minsn_t *temp = nlex->new_minsn(m_mul, res, f);
          res.create_from_insn(temp);                 // Probably this is not the best way, double-check later
          delete temp;
        }
      }

      return res;
    }

    //--------------------------------------------------------------------------
    const char *dstr() const
    {
      qstring s;
      s.sprnt("%d*[", coeff);
      for ( const auto &elem : prod )
      {
        s += "(";
        s += elem.first.dstr();
        s += "),";
      }
      s += "]";

      return s.c_str();
    }

    //--------------------------------------------------------------------------
    bool operator==(const term_t &right) const
    {
      return this->coeff == right.coeff && this->prod == right.prod;
    }
  };

  struct rule_t
  {
    qvector<term_t> src;
    mop_t dst;
  };
  //DECLARE_TYPE_AS_MOVABLE(rule_t);

  //--------------------------------------------------------------------------
  // Define the patterns for reversely applying the normalization rules
  class simp_patterns_t
  {
  public:
    qvector<rule_t> rules;

    //--------------------------------------------------------------------------
    simp_patterns_t(const std::set<mop_t> &vars, const nonlin_expr_t *nlex)
    {
      // build rules for one-variable expression
      if ( vars.size() == 1 )
      {
        mop_t x = *vars.begin();
        build_1v_rules(x, nlex);
        return;
      }

      // build rules for all two-variable combinations in vars
      qvector<const mop_t*> vars_vec;
      for ( const auto &e : vars )
        vars_vec.push_back(&e);
      for ( int i = 0; i < vars_vec.size(); i++ )
      {
        const mop_t &v1 = *vars_vec[i];
        for ( int j = i+1; j < vars_vec.size(); j++ )
        {
          const mop_t &v2 = *vars_vec[j];
          build_2v_rules(v1, v2, nlex);
          build_2v_rules(v2, v1, nlex);
        }
      }
    }

    //--------------------------------------------------------------------------
    // only one rule for 1 variable expression
    void build_1v_rules(const mop_t &x, const nonlin_expr_t *nlex)
    {
      // rule 10: -x - 1 --> not x
      term_t t1(x, -1);
      term_t t2(nlex->one, -1);
      rule_t &rule = rules.push_back();
      rule.src.push_back(t1);
      rule.src.push_back(t2);

      minsn_t *dst_ins = nlex->new_minsn(m_bnot, x);
      rule.dst = insn_to_mop(dst_ins);
    }

    //--------------------------------------------------------------------------
    void build_2v_rules(const mop_t &x, const mop_t &y, const nonlin_expr_t *nlex)
    {
      {
        // rule 1: y - (x and y) --> (not x) and y
        term_t t1(y, 1);
        term_t t2(nlex->new_minsn(m_and, x, y), -1);
        rule_t &rule = rules.push_back();
        rule.src.push_back(t1);
        rule.src.push_back(t2);

        minsn_t *dst_ins = nlex->new_minsn(m_and,
                                    nlex->new_minsn(m_bnot, x),
                                    y);
        rule.dst = insn_to_mop(dst_ins);
      }

      {
        // rule 2: x - (y and x) --> x and (not y)
        // already handled by rule 1
      }

      {
        // rule 3:
        // -x + (x and y) - 1 --> (not x) or y
        term_t t1(x, -1);
        term_t t2(nlex->new_minsn(m_and, x, y), 1);
        term_t t3(nlex->one, -1);
        rule_t &rule = rules.push_back();
        rule.src.push_back(t1);
        rule.src.push_back(t2);
        rule.src.push_back(t3);

        minsn_t *dst_ins = nlex->new_minsn(m_or,
                              nlex->new_minsn(m_bnot, x),
                              y);
        rule.dst = insn_to_mop(dst_ins);
      }

      {
        // rule 4: -y + (x and y) -1 --> x or (not y)
        // already handled by rule 3
      }

      {
        // rule 5: -x - y + (x and y) - 1 --> not (x or y)
        term_t t1(x, -1);
        term_t t2(y, -1);
        term_t t3(nlex->new_minsn(m_and, x, y), 1);
        term_t t4(nlex->one, -1);
        rule_t &rule = rules.push_back();
        rule.src.push_back(t1);
        rule.src.push_back(t2);
        rule.src.push_back(t3);
        rule.src.push_back(t4);

        minsn_t *dst_ins = nlex->new_minsn(m_bnot,
                              nlex->new_minsn(m_or, x, y));
        rule.dst = insn_to_mop(dst_ins);
      }

      {
        // rule 6: -x - y + 2*(x and y) - 1 --> not (x xor y)
        term_t t1(x, -1);
        term_t t2(y, -1);
        minsn_t *t3_ins = nlex->new_minsn(m_mul,
                            nlex->two,
                            nlex->new_minsn(m_and, x, y));
        term_t t3(insn_to_mop(t3_ins), 1);
        term_t t4(nlex->one, -1);
        rule_t &rule = rules.push_back();
        rule.src.push_back(t1);
        rule.src.push_back(t2);
        rule.src.push_back(t3);
        rule.src.push_back(t4);

        minsn_t *dst_ins = nlex->new_minsn(m_bnot,
                              nlex->new_minsn(m_xor, x, y));
        rule.dst = insn_to_mop(dst_ins);
      }

      {
        // rule 7: - (x and y) - 1 --> not (x and y)
        term_t t1(nlex->new_minsn(m_and, x, y), -1);
        term_t t2(nlex->one, -1);
        rule_t &rule = rules.push_back();
        rule.src.push_back(t1);
        rule.src.push_back(t2);

        minsn_t *dst_ins = nlex->new_minsn(m_bnot,
                              nlex->new_minsn(m_and, x, y));
        rule.dst = insn_to_mop(dst_ins);
      }

      {
        // rule 8: x + y - 2*(x and y) --> x xor y
        term_t t1(x, 1);
        term_t t2(y, 1);
        minsn_t *t3_ins = nlex->new_minsn(m_mul,
                            nlex->two,
                            nlex->new_minsn(m_and, x, y));
        term_t t3(insn_to_mop(t3_ins), -1);
        rule_t &rule = rules.push_back();
        rule.src.push_back(t1);
        rule.src.push_back(t2);
        rule.src.push_back(t3);

        minsn_t *dst_ins = nlex->new_minsn(m_xor, x, y);
        rule.dst = insn_to_mop(dst_ins);
      }

      {
        // rule 9: x + y - (x and y) --> x or y
        term_t t1(x, 1);
        term_t t2(y, 1);
        term_t t3(nlex->new_minsn(m_and, x, y), -1);
        rule_t &rule = rules.push_back();
        rule.src.push_back(t1);
        rule.src.push_back(t2);
        rule.src.push_back(t3);

        minsn_t *dst_ins = nlex->new_minsn(m_or, x, y);
        rule.dst = insn_to_mop(dst_ins);
      }

      {
        // rule 10: -x - 1 --> not x
        term_t t1(x, -1);
        term_t t2(nlex->one, -1);
        rule_t &rule = rules.push_back();
        rule.src.push_back(t1);
        rule.src.push_back(t2);

        minsn_t *dst_ins = nlex->new_minsn(m_bnot, x);
        rule.dst = insn_to_mop(dst_ins);
      }
    }
  };

  //--------------------------------------------------------------------------
  class normal_mba_t
  {
  public:
    qvector<term_t> terms;    // a normalized mba is a list of terms
    std::set<mop_t> vars;     // the variables in this mba expression
    qvector<mop_t> simp_res;

    //--------------------------------------------------------------------------
    // check the variables in terms and update vars
    bool update_vars()
    {
      vars.clear();
      for ( const term_t &t : terms )
      {
        const product_t &p = t.prod;
        for ( const auto &elem : p )
        {
          factor_t f = elem.first;
          if ( f.is_var() )
            vars.insert(f.op);
          else if ( f.is_and_expr() )
            vars.insert(f.ops.begin(), f.ops.end());
          else
            return false;
        }
      }
      return true;
    }

    //--------------------------------------------------------------------------
    // Final simplification by applying the normalization rules reversely
    // 1. y - (x and y) --> (not x) and y
    // 2. x - (x and y) --> x and (not y)
    // 3. -x + (x and y) - 1 --> (not x) or y
    // 4. -y + (x and y) - 1 --> x or (not y)
    // 5. -x - y + (x and y) - 1 --> not (x or y)
    // 6. -x - y + 2*(x and y) - 1 --> not (x xor y)
    // 7. - (x and y) - 1 --> not (x and y)
    // 8. x + y - 2*(x and y) --> x xor y
    // 9. x + y - (x and y) --> x or y
    // 10. - x - 1 --> not x
    void final_simplify(const simp_patterns_t &ptns)
    {
      for ( const rule_t &r : ptns.rules )
      {
        intvec_t matched;
        for ( size_t i=0; i < terms.size() && matched.size() < r.src.size(); i++ )
          if ( r.src.has(terms[i]) )            // matched
            matched.push_back(i);

        if ( matched.size() == r.src.size() )   // this rule has matched
        {
          simp_res.push_back(r.dst);
          for ( ssize_t i=matched.size()-1; i >= 0; i-- )
            terms.erase(terms.begin()+matched[i]);
        }
      }
    }

    //--------------------------------------------------------------------------
    // dump a normal mba expression as terms
    void dump()
    {
#ifdef LDEB
      for ( const auto &elem : terms )
        dmsg("%s, ", elem.dstr());
      dmsg("\n");
#endif
  }


  };

  normal_mba_t nm_mba;

public:
  //--------------------------------------------------------------------------
  nonlin_expr_t(const minsn_t &insn)
  {
    // prepare the constants before recursing, so it is done only once
    one.make_number(1, insn.l.size);
    two.make_number(2, insn.l.size);
    minus_one.make_number(-1, insn.l.size);

    cur_mba = new minsn_t(insn);

    dmsg("pre-processing ... \n");
    recur_preprocess(cur_mba);
    dmsg("%s\n", cur_mba->dstr());

    dmsg("normalizing ... \n");
    ok = recur_normalize(cur_mba);
    if ( !ok )
      return;
    dmsg("%s\n", cur_mba->dstr());

    dmsg("multiplication distribution ... \n");
    ok = recur_mul_dist(cur_mba);
    if ( !ok )
      return;
    dmsg("%s\n", cur_mba->dstr());

    dmsg("remove parenthesis ... \n");
    apply_rm_par(cur_mba);
    dmsg("%s\n", cur_mba->dstr());

    cur_mba->optimize_solo();

    if ( is_normalized(cur_mba) )
    {
      build_normal_mba(cur_mba, nm_mba);
      dmsg("Normalized MBA: ");
      nm_mba.dump();

      simp_nm_mba(nm_mba);
      dmsg("Simplification Result: ");
      nm_mba.dump();

      nm_mba.update_vars();
      simp_patterns_t ptns(nm_mba.vars, this);
      nm_mba.final_simplify(ptns);
      dmsg("Remaining terms after final_simp(): ");
      nm_mba.dump();
    }
    else
    {
      dmsg("The mba expr is not normal form!\n");
      ok = false;
    }

    // std::set<mop_t> ops;
    // ok = is_normalized(cur_mba, &ops);
    // dmsg("Dump all factors in the current MBA: ");
    // dump(ops);      // all factors in like-terms

    // remove equivalent factors
    // qvector<mop_t> vc;
    // for ( const mop_t &e: ops )
    //   vc.push_back(e);
    // for ( size_t i = 0; i < vc.size(); i++ )
    // {
    //   for ( size_t j = i+1; j < vc.size(); j++ )
    //     if ( eq_mba_product(vc[i], vc[j]) )
    //     {
    //       vc.erase(vc.begin() + j);
    //       j--;
    //     }
    // }
    // dmsg("Factors after removing equivalent ones: ");
    // dump(vc);

    // ok = false; // disabled non-linear optimization because the tests fail with it
  }

  ~nonlin_expr_t() { delete cur_mba; }
  bool success() const { return ok; }

  //--------------------------------------------------------------------------
  // recursively apply mul_dist() to ins
  bool recur_mul_dist(minsn_t *ins)
  {
    // dmsg("recur_mul_dist on: %s\n", ins->dstr());
    mul_dist(ins);
    if ( ins->l.is_insn() )
      recur_mul_dist(ins->l.d);
    if ( ins->r.is_insn() )
      recur_mul_dist(ins->r.d);
    return true;
  }

  //--------------------------------------------------------------------------
  // Apply multiplicative distribution law to ins
  // (a+b)*c => a*c+b*c
  bool mul_dist(minsn_t *ins) const
  {
    if ( ins->opcode != m_mul )
      return false;

    if ( !ins->l.is_insn() && !ins->r.is_insn() )
      return false; // at least one operand must be an instruction

    mop_t *c;
    minsn_t *addsub;
    if ( ins->r.is_insn(m_add) || ins->r.is_insn(m_sub) )
    {
      c = &ins->l;
      addsub = ins->r.d;
    }
    else if ( ins->l.is_insn(m_add) || ins->l.is_insn(m_sub) )
    {
      addsub = ins->l.d;
      c = &ins->r;
    }
    else
    {
      return false; // could not locate 'c'
    }

    minsn_t *ins_a = new_minsn(m_mul, addsub->l, *c);
    minsn_t *ins_b = new_minsn(m_mul, addsub->r, *c);     // Note that operand order could change: c*(a +/- b) becomes a*c +/- b*c

    ins->opcode = addsub->opcode;
    ins->l = insn_to_mop(ins_a);
    ins->r = insn_to_mop(ins_b);
    return true;
  }

  //--------------------------------------------------------------------------
  // recursively apply the pre-process rules to ins
  void recur_preprocess(minsn_t *ins, minsn_t *parent=nullptr)
  {
    preprocess(ins, parent);
    if ( ins->l.is_insn() )
      recur_preprocess(ins->l.d, ins);
    if ( ins->r.is_insn() )
      recur_preprocess(ins->r.d, ins);
  }

  //--------------------------------------------------------------------------
  // do some pattern replacing
  void preprocess(minsn_t *ins, minsn_t *parent)
  {
    cur_ea = ins->ea; // will be used by new_minsn()
    minsn_t *sub;

    if ( parent != nullptr && parent->opcode == m_and && ins->opcode == m_sub && ins->r.signed_value() == 1 )      // (x - 1) --> ~ -x
    {
      mop_t &x = ins->l;
      // ~ -x
      sub = new_minsn(m_bnot, new_minsn(m_neg, x));
      ins->swap(*sub);
      delete sub;
    }

  }

  //--------------------------------------------------------------------------
  // recursively apply normalization to ins
  bool recur_normalize(minsn_t *ins)
  {
    // dmsg("recur_normalize on: %s\n", ins->dstr());
    normalize(ins);
    if ( ins->l.is_insn() )
      recur_normalize(ins->l.d);
    if ( ins->r.is_insn() )
      recur_normalize(ins->r.d);
    return true;
  }

  //--------------------------------------------------------------------------
  // Normalize any boolean expr to a simple MBA with only x, y, x and y, and constants
  // Rules:
  // 1. (not x) and y = y - (x and y)
  // 2. x and (not y): swap left and right, then use rule 1
  // 3. (not x) or y  = -x + (x and y) - 1
  // 4. x or (not y): swap left and right, then use rule 8
  // 5. not (x or y)  = -x - y + (x and y) - 1
  // 6. not (x xor y) = -x - y + 2*(x and y) - 1
  // 7. not (x and y) = - (x and y) - 1
  // 8. x xor y = x + y - 2*(x and y)
  // 9. x or y  = x + y - (x and y)
  // 10. not x  = - x - 1
  bool normalize(minsn_t *ins)
  {
    cur_ea = ins->ea; // will be used by new_minsn()

    minsn_t *sub;
    switch ( match(ins) )
    {
      // Rules 1 and 2
      case 2:
        ins->l.swap(ins->r);
        // no break
      case 1:
        {
          mop_t &x = ins->l.d->l;
          mop_t &y = ins->r;
          // y - (x and y)
          sub = new_minsn(m_sub, y,
                  new_minsn(m_and, x, y));
        }
        break;

      // Rules 3 and 4
      case 4:
        ins->l.swap(ins->r);
        // no break
      case 3:
        {
          mop_t &x = ins->l.d->l;
          mop_t &y = ins->r;
          // -1 * x + (x and y) - 1
          sub = new_minsn(m_sub,
                  new_minsn(m_add,
                    new_minsn(m_mul, minus_one, x),
                    new_minsn(m_and, x, y)),
                  one);
        }
        break;

      // Rule 5
      case 5:
        {
          mop_t &x = ins->l.d->l;
          mop_t &y = ins->l.d->r;
          // -1 * x - y + (x and y) - 1
          sub = new_minsn(m_sub,
                  new_minsn(m_add,
                    new_minsn(m_sub, new_minsn(m_mul, minus_one, x), y),
                    new_minsn(m_and, x, y)),
                  one);
        }
        break;

      // Rule 6
      case 6:
        {
          mop_t &x = ins->l.d->l;
          mop_t &y = ins->l.d->r;
          // -1 * x - y + 2*(x and y) - 1
          sub = new_minsn(m_sub,
                  new_minsn(m_add,
                    new_minsn(m_sub, new_minsn(m_mul, minus_one, x), y),
                    new_minsn(m_mul, two, new_minsn(m_and, x, y))),
                  one);
        }
        break;

      // Rule 7
      case 7:
        {
          mop_t &x = ins->l.d->l;
          mop_t &y = ins->l.d->r;
          // -1 * (x and y) - 1
          sub = new_minsn(m_sub,
                  new_minsn(m_mul, minus_one, new_minsn(m_and, x, y)),
                  one);
        }
        break;

      // Rule 8
      case 8:
        {
          mop_t &x = ins->l;
          mop_t &y = ins->r;
          // x + y - 2*(x and y)
          sub = new_minsn(m_sub,
                  new_minsn(m_add, x, y),
                  new_minsn(m_mul, two, new_minsn(m_and, x, y)));
        }
        break;

      // Rule 9
      case 9:
        {
          mop_t &x = ins->l;
          mop_t &y = ins->r;
          // x + y - (x and y)
          sub = new_minsn(m_sub,
                  new_minsn(m_add, x, y),
                  new_minsn(m_and, x, y));
        }
        break;

      // Rule 10
      case 10:
        {
          mop_t &x = ins->l;
          // -1 * x - 1
          sub = new_minsn(m_sub,
                  new_minsn(m_mul, minus_one, x),
                  one);
        }
        break;

      // // Rule 11
      // case 11:
      //   {
      //     mop_t &x = ins->l;
      //     // -1 * x
      //     sub = new_minsn(m_mul, minus_one, x);
      //   }
      //   break;

      default:
        return false;
    }

    ins->swap(*sub);
    delete sub;
    return true;
  }

  //--------------------------------------------------------------------------
  // optimized code that can handle only the special case when:
  //   - op is empty
  //   - ins is a simple arithmetic instruction with no d operand (but with d.size)
  // this function takes ownership of 'ins'
  static mop_t insn_to_mop(minsn_t *ins)
  {
    mop_t mop;
    mop._make_insn(ins);
    mop.size = ins->d.size;
    return mop;
  }

  //--------------------------------------------------------------------------
  // Six overloaded functions for creating new minsn from mop or sub-minsn.
  // Four functions for creating binary-op instructions
  minsn_t *new_minsn(mcode_t mc, minsn_t *left, minsn_t *right) const
  {
    return new_minsn(mc, insn_to_mop(left), insn_to_mop(right));
  }

  //--------------------------------------------------------------------------
  minsn_t *new_minsn(mcode_t mc, const mop_t &left, minsn_t *right) const
  {
    return new_minsn(mc, left, insn_to_mop(right));
  }

  //--------------------------------------------------------------------------
  minsn_t *new_minsn(mcode_t mc, minsn_t *left, const mop_t &right) const
  {
    return new_minsn(mc, insn_to_mop(left), right);
  }

  //--------------------------------------------------------------------------
  minsn_t *new_minsn(mcode_t mc, const mop_t &left, const mop_t &right) const
  {
    minsn_t *ins = new minsn_t(cur_ea);
    ins->opcode = mc;
    ins->l = left;
    ins->r = right;
    ins->d.size = left.size;
    return ins;
  }

  //--------------------------------------------------------------------------
  // Two functions for creating unary-op instructions
  minsn_t *new_minsn(mcode_t mc, minsn_t *left) const
  {
    return new_minsn(mc, insn_to_mop(left));
  }

  //--------------------------------------------------------------------------
  minsn_t *new_minsn(mcode_t mc, const mop_t &left) const
  {
    minsn_t *ins = new minsn_t(cur_ea);
    ins->opcode = mc;
    ins->l = left;
    ins->d.size = left.size;
    return ins;
  }

  //--------------------------------------------------------------------------
  static int match(const minsn_t *ins)
  {
    // Attention: the order of matching these rules matters!!!
    if ( ins->opcode == m_and && ins->l.is_insn(m_bnot) )  // 1. (not x) and y
      return 1;
    if ( ins->opcode == m_and && ins->r.is_insn(m_bnot) )  // 2. x and (not y)
      return 2;
    if ( ins->opcode == m_or && ins->l.is_insn(m_bnot) )   // 3. (not x) or y
      return 3;
    if ( ins->opcode == m_or && ins->r.is_insn(m_bnot) )   // 4. x or (not y)
      return 4;
    if ( ins->opcode == m_bnot && ins->l.is_insn(m_or) )   // 5. not (x or y)
      return 5;
    if ( ins->opcode == m_bnot && ins->l.is_insn(m_xor) )  // 6. not (x xor y)
      return 6;
    if ( ins->opcode == m_bnot && ins->l.is_insn(m_and) )  // 7. not (x and y)
      return 7;
    if ( ins->opcode == m_xor )                            // 8. x xor y
      return 8;
    if ( ins->opcode == m_or )                             // 9. x or y
      return 9;
    if ( ins->opcode == m_bnot )                           // 10. not x
      return 10;
    // if ( ins->opcode == m_neg )                            // 11. -x  --> -1 * x
    //   return 11;
    return 0;
  }

  //--------------------------------------------------------------------------
  // Remove the parentheses in the math expression stored in ins
  // E.g., a-(b+c) = a-b-c
  minsn_t *rm_par(minsn_t *ins)
  {
    minsn_t *rst = ins->r.d;   // ins's right sub-tree
    mop_t &a = ins->l;
    mop_t &b = rst->l;
    mop_t &c = rst->r;

    if ( ins->opcode == m_sub )   // flip the add/sub in a-(b+c), a-(b-c)
      rst->opcode = rst->opcode == m_add ? m_sub : m_add;

    minsn_t *sub = new_minsn(rst->opcode,
                    new_minsn(ins->opcode, a, b),
                    c);

    ins->swap(*sub);
    delete sub;

    return ins;
  }

  //--------------------------------------------------------------------------
  // Apply rm_par on all sub-expressions of ins. The result is somehow like
  // a left-skewed tree that is easy for extracting and merging like-terms.
  void apply_rm_par(minsn_t *ins)
  {
    // Process the current ins until the right sub-tree does not fit, then
    // go to the left sub-tree
    while ( (ins->opcode == m_add || ins->opcode == m_sub)        // a+(b+c), a+(b-c), a-(b+c), a-(b-c)
         && (ins->r.is_insn(m_add) || ins->r.is_insn(m_sub)) )
      ins = rm_par(ins);

    if ( ins->l.is_insn() )
      apply_rm_par(ins->l.d);
  }

  //--------------------------------------------------------------------------
  // Check an operand is an MBA product, i.e., the product of a variable with their AND expressions.
  // like: (x&y) * x * (x&y) * y
  static bool is_product(const mop_t &op)
  {
    if ( op.t == mop_S || op.is_reg() )
      return true;
    if ( op.is_insn() )
    {
      if ( op.d->opcode == m_neg || op.d->opcode == m_add || op.d->opcode == m_sub )    // allow minus, add, sub expr as a factor
        return true;
      else if ( op.d->opcode == m_mul || op.d->opcode == m_and )
        return is_product(op.d->l) && is_product(op.d->r);
      else
        return false;
    }
    else
      return false;
  }

  //--------------------------------------------------------------------------
  // Check an operand is a normalized mba term, which is a constant, variable, or a mba factor
  // with/without coefficient.
  // Examples:
  // x, 3*x*y*(x&y)
  // The optional argument s is a set that collects the non-coefficient parts of a term
  static bool is_mba_term(const mop_t &op, std::set<mop_t> *s=nullptr)
  {
    if ( op.is_constant() )                                 // constant term
      return true;
    if ( op.t == mop_S || op.is_reg() )                // single variable term
    {
      if ( s != nullptr )
        s->insert(op);
      return true;
    }
    if ( is_product(op) )                           // no coefficient
    {
      if ( s != nullptr )
        s->insert(op);
      return true;
    }
    if ( op.is_insn() && op.d->opcode == m_mul )
    {
      minsn_t *ins = op.d;
      if ( ins->l.is_constant() && is_product(ins->r) )          // n*(a&b)
      {
        if ( s != nullptr )
          s->insert(ins->r);
        return true;
      }
      if ( ins->r.is_constant() && is_product(ins->l) )     // (a&b)*n
      {
        if ( s != nullptr )
          s->insert(ins->l);
        return true;
      }
    }
    return false;
  }

  //--------------------------------------------------------------------------
  // Check if an MBA has been successfully normalized
  static bool is_normalized(minsn_t *minsn, std::set<mop_t> *s=nullptr)
  {
    minsn_t *ins = minsn;
    // check every insn node is a left subtree +/- a mba_term
    while ( ins->l.is_insn() && (ins->l.d->opcode == m_add || ins->l.d->opcode == m_sub) )
    {
      if ( (ins->opcode == m_add || ins->opcode == m_sub) && is_mba_term(ins->r, s) )
        ins = ins->l.d;
      else
        return false;
    }

    // Check the left most term
    if ( (ins->opcode == m_add || ins->opcode == m_sub)
      && is_mba_term(ins->r, s)
      && is_mba_term(ins->l, s) )
    {
      return true;
    }
    return false;
  }


  //--------------------------------------------------------------------------
  // Recursively collect all variables in the sub-instructions of an operand op,
  // and put them into a set. Here it is only used for the AND expression.
  static bool build_op_set(mop_t op, std::set<mop_t> *op_set)
  {
    if ( op.t == mop_S || op.is_reg() )             // single variables
    {
      op_set->insert(op);
      return true;
    }
    if ( op.is_insn() )
    {
      if ( op.d->opcode == m_neg || op.d->opcode == m_add      // allow minus, add, sub expr as a factor
        || op.d->opcode == m_sub )
      {
        op_set->insert(op);
        return true;
      }
      else
        return build_op_set(op.d->l, op_set) && build_op_set(op.d->r, op_set);
    }
    else
      return false;
  }

  //--------------------------------------------------------------------------
  static bool build_product(product_t *p, mop_t op)
  {
    if ( op.t == mop_S || op.is_reg() )
    {
      factor_t f(op);
      (*p)[f]++;
      return true;
    }
    if ( op.is_insn() )
    {
      if ( op.d->opcode == m_mul )
        return build_product(p, op.d->l) && build_product(p, op.d->r);
      if ( op.d->opcode == m_and )
      {
        std::set<mop_t> and_expr;
        build_op_set(op, &and_expr);
        factor_t f(and_expr, op);
        (*p)[f]++;
        return true;
      }
      if ( op.d->opcode == m_neg )      // allow -x as a factor
      {
        factor_t f(op);
        (*p)[f]++;
        return true;
      }
    }
    return false;
  }

  //--------------------------------------------------------------------------
  // Check whether two mba products are equivalent. It is used for getting the like-terms.
  static bool eq_mba_product(const mop_t &op1, const mop_t &op2)
  {
    if ( op1.t == mop_S && op2.t == mop_S )
      return op1 == op2;
    if ( op1.is_reg() && op2.is_reg() )
      return op1 == op2;
    if ( op1.is_insn() && op2.is_insn() )
    {
      product_t p1, p2;
      if ( build_product(&p1, op1) && build_product(&p2, op2) )
        return p1 == p2;
      dmsg("build_product error!\n");
      INTERR(0);
    }
    return false;
  }

  //--------------------------------------------------------------------------
  static bool build_normal_mba(minsn_t *ins, normal_mba_t &nm_mba)
  {
    while ( ins->l.is_insn() && (ins->l.d->opcode == m_add || ins->l.d->opcode == m_sub) )
    {
      if ( ins->opcode == m_add )
      {
        term_t new_term(ins->r, 1);
        nm_mba.terms.push_back(new_term);
      }
      else if ( ins->opcode == m_sub )
      {
        term_t new_term(ins->r, -1);
        nm_mba.terms.push_back(new_term);
      }
      else
      {
        dmsg("build_normal_mba error: unrecognized term.");
        return false;
      }

      ins = ins->l.d;
    }

    // add the leftmost term and the second leftmost one
    if ( ins->opcode == m_add )
    {
      term_t new_term_r(ins->r, 1);
      term_t new_term_l(ins->l, 1);         // TODO: the leftmost coefficient could be negative
      nm_mba.terms.push_back(new_term_r);
      nm_mba.terms.push_back(new_term_l);
      return true;
    }
    else if ( ins->opcode == m_sub )
    {
      term_t new_term_r(ins->r, -1);
      term_t new_term_l(ins->l, 1);
      nm_mba.terms.push_back(new_term_r);
      nm_mba.terms.push_back(new_term_l);
      return true;
    }
    dmsg("build_normal_mba error: unrecognized leftmost term.");
    return false;
  }

  //--------------------------------------------------------------------------
  // Simplify a normalized mba
  static bool simp_nm_mba(normal_mba_t &nm_mba)
  {
    auto &terms = nm_mba.terms;
    for ( size_t i = 0; i < terms.size(); i++ )
    {
      for ( size_t j = i+1; j < terms.size(); j++ )
      {
        if ( terms[i].prod == terms[j].prod )       // like-terms
        {
          terms[i].coeff += terms[j].coeff;         // merge like-terms
          terms.erase(terms.begin() + j);
          j--;
        }
      }
    }

    for ( size_t i = 0; i < terms.size(); i++ )
    {
      if ( terms[i].coeff == 0 )                    // Delete the terms whose coefficient is 0
      {
        terms.erase(terms.begin() + i);
        i--;
      }
    }

    return true;
  }

  //--------------------------------------------------------------------------
  // dump the elements in a set of mop_t
  static void dump(const std::set<mop_t> &ops)
  {
#ifdef LDEB
    for ( const mop_t &elem : ops )
      dmsg("%s ", elem.dstr());
    dmsg("\n");
#else
    qnotused(ops);
#endif
  }

  //--------------------------------------------------------------------------
  // dump the elements in a vector of mop_t
  static void dump(const qvector<mop_t> &ops)
  {
#ifdef LDEB
    for ( const mop_t &elem : ops )
      dmsg("%s ", elem.dstr());
    dmsg("\n");
#else
    qnotused(ops);
#endif
  }

  //--------------------------------------------------------------------------
  minsn_t *to_minsn(ea_t ea) const override
  {
    int size = one.size;
    minsn_t *res = new minsn_t(ea);
    res->opcode = m_ldc;
    res->l.make_number(0, size, ea);  // Use 0 as the init value, like const_term in linear_exprs.cpp
    res->d.size = size;

    // generate minsn for the terms
    for ( const term_t &term : nm_mba.terms )
    {
      // add terms
      minsn_t *add = new minsn_t(ea);
      add->opcode = m_add;
      add->l.t = mop_d;
      add->l.d = res;
      add->l.size = size;
      add->r = term.to_mop(this);
      add->d.size = size;
      res = add;
    }

    // generate minsn for the terms simplified by the final_simplify()
    for ( const mop_t &op : nm_mba.simp_res )
    {
      minsn_t *add = new minsn_t(ea);
      add->opcode = m_add;
      add->l.t = mop_d;
      add->l.d = res;
      add->l.size = size;
      add->r = op;
      add->d.size = size;
      res = add;
    }

    return res;
  }

  //--------------------------------------------------------------------------
  // The following functions are only place holders.
  const char *dstr() const override
  {
    // Not implemented yet
    static char buf[MAXSTR];
    return buf;
  }

  //--------------------------------------------------------------------------
  mcode_val_t evaluate(mcode_emulator_t &emu) const override
  {
    mcode_val_t res = emu.minsn_value(*cur_mba);
    return res;
  }

  //--------------------------------------------------------------------------
  z3::expr to_smt(z3_converter_t &cvtr) const override
  {
    z3::expr res = cvtr.minsn_to_expr(*cur_mba);
    return res;
  }
};
