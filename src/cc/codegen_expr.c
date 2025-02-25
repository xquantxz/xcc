#include "../config.h"
#include "codegen.h"

#include <assert.h>
#include <stdlib.h>  // malloc

#include "ast.h"
#include "ir.h"
#include "regalloc.h"
#include "table.h"
#include "type.h"
#include "util.h"
#include "var.h"

#include "parser.h"  // curfunc

bool is_stack_param(const Type *type) {
  return type->kind == TY_STRUCT;
}

VRegType *to_vtype(const Type *type) {
  assert(type->kind != TY_VOID);
  VRegType *vtype = malloc(sizeof(*vtype));
  vtype->size = type_size(type);
  vtype->align = align_size(type);

  int flag = 0;
  bool is_unsigned = is_fixnum(type->kind) ? type->fixnum.is_unsigned : true;
#ifndef __NO_FLONUM
  if (is_flonum(type)) {
    flag |= VRTF_FLONUM;
    is_unsigned = false;
  }
#endif
  if (is_unsigned)
    flag |= VRTF_UNSIGNED;
  if (is_stack_param(type))
    flag |= VRTF_NON_REG;
  vtype->flag = flag;

  return vtype;
}

VReg *add_new_reg(const Type *type, int flag) {
  return reg_alloc_spawn(((FuncBackend*)curfunc->extra)->ra, to_vtype(type), flag);
}

static Table builtin_function_table;  // <BuiltinFunctionProc>

void add_builtin_function(const char *str, Type *type, BuiltinFunctionProc *proc, bool add_to_scope) {
  const Name *name = alloc_name(str, NULL, false);
  table_put(&builtin_function_table, name, proc);

  if (add_to_scope)
    scope_add(global_scope, name, type, 0);
}

static enum ConditionKind swap_cond(enum ConditionKind cond) {
  assert(COND_EQ <= cond && cond <= COND_GT);
  if (cond >= COND_LT)
    cond = (COND_GT + COND_LT) - cond;
  return cond;
}

static enum ConditionKind gen_compare_expr(enum ExprKind kind, Expr *lhs, Expr *rhs) {
  assert(lhs->type->kind == rhs->type->kind);

  assert(EX_EQ <= kind && kind <= EX_GT);
  enum ConditionKind cond = kind + (COND_EQ - EX_EQ);
  if (is_const(lhs)) {
    assert(!is_const(rhs));
    Expr *tmp = lhs;
    lhs = rhs;
    rhs = tmp;
    cond = swap_cond(cond);
  }

  int flag = 0;
  if ((is_fixnum(lhs->type->kind) && lhs->type->fixnum.is_unsigned) ||
       lhs->type->kind == TY_PTR) {
    // unsigned
    flag = COND_UNSIGNED;
  }
#ifndef __NO_FLONUM
  if (is_flonum(lhs->type))
    flag |= COND_FLONUM;
#endif

  VReg *lhs_reg = gen_expr(lhs);
  VReg *rhs_reg = gen_expr(rhs);
  if ((rhs_reg->flag & VRF_CONST) != 0 && (lhs_reg->flag & VRF_CONST) != 0) {
#ifndef __NO_FLONUM
    // Const VReg is must be non-flonum.
    assert(!(lhs_reg->vtype->flag & VRTF_FLONUM));
    assert(!(rhs_reg->vtype->flag & VRTF_FLONUM));
    assert(!(flag & COND_FLONUM));
#endif
    switch (cond | flag) {
    case COND_NONE:
    case COND_ANY:
      return cond;
    case COND_EQ:  return lhs_reg->fixnum == rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_NE:  return lhs_reg->fixnum != rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_LT:  return lhs_reg->fixnum <  rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_LE:  return lhs_reg->fixnum <= rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_GE:  return lhs_reg->fixnum >= rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_GT:  return lhs_reg->fixnum >  rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_EQ | COND_UNSIGNED:  return (uint64_t)lhs_reg->fixnum == (uint64_t)rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_NE | COND_UNSIGNED:  return (uint64_t)lhs_reg->fixnum != (uint64_t)rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_LT | COND_UNSIGNED:  return (uint64_t)lhs_reg->fixnum <  (uint64_t)rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_LE | COND_UNSIGNED:  return (uint64_t)lhs_reg->fixnum <= (uint64_t)rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_GE | COND_UNSIGNED:  return (uint64_t)lhs_reg->fixnum >= (uint64_t)rhs_reg->fixnum ? COND_ANY : COND_NONE;
    case COND_GT | COND_UNSIGNED:  return (uint64_t)lhs_reg->fixnum >  (uint64_t)rhs_reg->fixnum ? COND_ANY : COND_NONE;
    default: assert(false); break;
    }
  }

  switch (lhs->type->kind) {
  case TY_FIXNUM: case TY_PTR:
#ifndef __NO_FLONUM
  case TY_FLONUM:
#endif
    break;
  default: assert(false); break;
  }

  new_ir_cmp(lhs_reg, rhs_reg);
  return cond | flag;
}

void gen_cond_jmp(Expr *cond, bool tf, BB *bb) {
  enum ExprKind ck = cond->kind;
  switch (ck) {
  case EX_FIXNUM:
    if (cond->fixnum == 0)
      tf = !tf;
    if (tf)
      new_ir_jmp(COND_ANY, bb);
    return;
  case EX_EQ:
  case EX_NE:
  case EX_LT:
  case EX_LE:
  case EX_GE:
  case EX_GT:
    if (!tf) {
      if (ck <= EX_NE)
        ck = (EX_EQ + EX_NE) - ck;  // EQ <-> NE
      else
        ck = EX_LT + ((ck - EX_LT) ^ 2);  // LT <-> GE, LE <-> GT
    }
    new_ir_jmp(gen_compare_expr(ck, cond->bop.lhs, cond->bop.rhs), bb);
    return;
  case EX_LOGAND:
  case EX_LOGIOR:
    {
      BB *bb1 = new_bb();
      BB *bb2 = new_bb();
      if (!tf)
        ck = (EX_LOGAND + EX_LOGIOR) - ck;  // LOGAND <-> LOGIOR
      if (ck == EX_LOGAND) {
        gen_cond_jmp(cond->bop.lhs, !tf, bb2);
        set_curbb(bb1);
        gen_cond_jmp(cond->bop.rhs, tf, bb);
      } else {
        gen_cond_jmp(cond->bop.lhs, tf, bb);
        set_curbb(bb1);
        gen_cond_jmp(cond->bop.rhs, tf, bb);
      }
      set_curbb(bb2);
    }
    return;
  case EX_COMMA:
    gen_expr(cond->bop.lhs);
    gen_cond_jmp(cond->bop.rhs, tf, bb);
    break;
  default:
    assert(false);
    break;
  }
}

static VReg *gen_cast(VReg *reg, const Type *dst_type) {
  switch (dst_type->kind) {
  case TY_VOID:
    return NULL;  // Assume void value is not used.
  case TY_STRUCT:
    return reg;
  default: break;
  }

  if (reg->flag & VRF_CONST) {
#ifndef __NO_FLONUM
    assert(!(reg->vtype->flag & VRTF_FLONUM));  // No const vreg for flonum.
#endif
    Fixnum value = reg->fixnum;
    size_t dst_size = type_size(dst_type);
    if (dst_size < (size_t)reg->vtype->size && dst_size < sizeof(Fixnum)) {
      // Assume that integer is represented in Two's complement
      size_t bit = dst_size * TARGET_CHAR_BIT;
      UFixnum mask = (-1UL) << bit;
      if (dst_type->kind == TY_FIXNUM && !dst_type->fixnum.is_unsigned &&  // signed
          (value & (1 << (bit - 1))))  // negative
        value |= mask;
      else
        value &= ~mask;
    }

    VRegType *vtype = to_vtype(dst_type);
    return new_const_vreg(value, vtype);
  }

  int dst_size = type_size(dst_type);
  bool lu = dst_type->kind == TY_FIXNUM ? dst_type->fixnum.is_unsigned : dst_type->kind == TY_PTR;
  bool ru = (reg->vtype->flag & VRTF_UNSIGNED) ? true : false;
  if (dst_size == reg->vtype->size && lu == ru
#ifndef __NO_FLONUM
      && is_flonum(dst_type) == ((reg->vtype->flag & VRTF_FLONUM) != 0)
#endif
  )
    return reg;

  return new_ir_cast(reg, to_vtype(dst_type));
}

static VReg *gen_lval(Expr *expr) {
  switch (expr->kind) {
  case EX_VAR:
    {
      Scope *scope;
      const VarInfo *varinfo = scope_find(expr->var.scope, expr->var.name, &scope);
      assert(varinfo != NULL && scope == expr->var.scope);
      if (is_global_scope(scope))
        return new_ir_iofs(expr->var.name, (varinfo->storage & VS_STATIC) == 0);
      if (varinfo->storage & VS_STATIC)
        return new_ir_iofs(varinfo->static_.gvar->name, false);
      if (varinfo->storage & VS_EXTERN)
        return new_ir_iofs(expr->var.name, true);
      return new_ir_bofs(varinfo->local.reg);
    }
  case EX_DEREF:
    return gen_expr(expr->unary.sub);
  case EX_MEMBER:
    {
      const MemberInfo *member = member_info(expr);
      VReg *reg = gen_expr(expr->member.target);
      if (member->offset == 0)
        return reg;
      VRegType *vtype = to_vtype(&tySize);
      VReg *imm = new_const_vreg(member->offset, vtype);
      VReg *result = new_ir_bop(IR_ADD, reg, imm, vtype);
      return result;
    }
  case EX_COMPLIT:
    {
      Expr *var = expr->complit.var;
      assert(var->var.scope != NULL);
      const VarInfo *varinfo = scope_find(var->var.scope, var->var.name, NULL);
      assert(varinfo != NULL);
      assert(varinfo->local.reg != NULL);
      varinfo->local.reg->flag |= VRF_REF;

      gen_clear_local_var(varinfo);
      gen_stmts(expr->complit.inits);
      return gen_lval(expr->complit.var);
    }
  default:
    assert(false);
    break;
  }
  return NULL;
}

static VReg *gen_variable(Expr *expr) {
  switch (expr->type->kind) {
  case TY_FIXNUM:
  case TY_PTR:
#ifndef __NO_FLONUM
  case TY_FLONUM:
#endif
    {
      Scope *scope;
      const VarInfo *varinfo = scope_find(expr->var.scope, expr->var.name, &scope);
      assert(varinfo != NULL && scope == expr->var.scope);
      if (!is_global_scope(scope) && !(varinfo->storage & (VS_STATIC | VS_EXTERN))) {
        assert(varinfo->local.reg != NULL);
        return varinfo->local.reg;
      }

      VReg *reg = gen_lval(expr);
      VReg *result = new_ir_unary(IR_LOAD, reg, to_vtype(expr->type));
      return result;
    }
  default:
    assert(false);
    // Fallthrough to suppress compile error.
  case TY_ARRAY:   // Use variable address as a pointer.
  case TY_STRUCT:  // struct value is handled as a pointer.
  case TY_FUNC:
    return gen_lval(expr);
  }
}

static VReg *gen_ternary(Expr *expr) {
  BB *tbb = new_bb();
  BB *fbb = new_bb();
  BB *nbb = new_bb();
  VReg *result = NULL;
  if (expr->type->kind != TY_VOID) {
    if (is_number(expr->type) || ptr_or_array(expr->type))
      result = add_new_reg(expr->type, 0);
    else
      result = add_new_reg(ptrof(expr->type), 0);
  }

  gen_cond_jmp(expr->ternary.cond, false, fbb);

  set_curbb(tbb);
  VReg *tval = gen_expr(expr->ternary.tval);
  if (result != NULL)
    new_ir_mov(result, tval);
  new_ir_jmp(COND_ANY, nbb);

  set_curbb(fbb);
  VReg *fval = gen_expr(expr->ternary.fval);
  if (result != NULL)
    new_ir_mov(result, fval);

  set_curbb(nbb);
  return result;
}

typedef struct {
  int reg_index;
  int offset;
  int size;
  bool stack_arg;
#ifndef __NO_FLONUM
  bool is_flonum;
#endif
} ArgInfo;

static VReg *gen_funcall(Expr *expr) {
  Expr *func = expr->funcall.func;
  if (func->kind == EX_VAR && is_global_scope(func->var.scope)) {
    void *proc = table_get(&builtin_function_table, func->var.name);
    if (proc != NULL)
      return (*(BuiltinFunctionProc*)proc)(expr);
  }
  const Type *functype = get_callee_type(func->type);
  assert(functype != NULL);

  Vector *args = expr->funcall.args;
  int arg_count = args->len;

  int offset = 0;

  VReg *retvar_reg = NULL;  // Return value is on the stack.
  if (is_stack_param(expr->type)) {
    const Name *name = alloc_label();
    VarInfo *ret_varinfo = scope_add(curscope, name, expr->type, 0);
    ret_varinfo->local.reg = retvar_reg = add_new_reg(expr->type, 0);
  }

  int total_arg_count = arg_count + (retvar_reg != NULL ? 1 : 0);
  VRegType **arg_vtypes = (retvar_reg == NULL && arg_count <= 0) ? NULL :
    calloc(total_arg_count, sizeof(*arg_vtypes));

  ArgInfo *arg_infos = NULL;
  int stack_arg_count = 0;
  int reg_arg_count = 0;
  {
    int arg_start = retvar_reg != NULL ? 1 : 0;
    int ireg_index = arg_start;
#ifndef __NO_FLONUM
    int freg_index = 0;
#endif

    // Check stack arguments.
    arg_infos = ALLOCA(sizeof(*arg_infos) * arg_count);
    for (int i = 0; i < arg_count; ++i) {
      ArgInfo *p = &arg_infos[i];
      p->reg_index = -1;
      p->offset = -1;
      Expr *arg = args->data[i];
      assert(arg->type->kind != TY_ARRAY);
      p->size = type_size(arg->type);
#ifndef __NO_FLONUM
      p->is_flonum = is_flonum(arg->type);
#endif
      p->stack_arg = is_stack_param(arg->type);
#if defined(VAARG_ON_STACK)
      if (functype->func.vaargs && functype->func.params != NULL && i >= functype->func.params->len)
        p->stack_arg = true;
#endif
      bool reg_arg = !p->stack_arg;
      if (reg_arg) {
#ifndef __NO_FLONUM
        if (p->is_flonum)
          reg_arg = freg_index < MAX_FREG_ARGS;
        else
#endif
          reg_arg = ireg_index < MAX_REG_ARGS;
      }
      if (!reg_arg) {
        offset = ALIGN(offset, align_size(arg->type));
        p->offset = offset;
        offset += ALIGN(p->size, WORD_SIZE);
        ++stack_arg_count;
      } else {
        ++reg_arg_count;
#ifndef __NO_FLONUM
        if (p->is_flonum)
          p->reg_index = freg_index++;
        else
#endif
          p->reg_index = ireg_index++;
      }
    }

    for (int i = 0; i < arg_count; ++i) {
      Expr *arg = args->data[i];
      arg_vtypes[i + arg_start] = to_vtype(arg->type);
    }
  }
  offset = ALIGN(offset, 16);

  IR *precall = new_ir_precall(arg_count - stack_arg_count, offset);

  if (offset > 0)
    new_ir_subsp(new_const_vreg(offset, to_vtype(&tySSize)), NULL);

  {
    // Register arguments.
    int iregarg = 0;
    for (int i = arg_count; --i >= 0; ) {
      Expr *arg = args->data[i];
      VReg *reg = gen_expr(arg);
      const ArgInfo *p = &arg_infos[i];
      if (p->offset < 0) {
        new_ir_pusharg(reg);
        ++iregarg;
      } else {
        VRegType offset_type = {.size = 4, .align = 4, .flag = 0};  // TODO:
        int ofs = p->offset + iregarg * REGARG_SIZE;
        VReg *dst = new_ir_sofs(new_const_vreg(ofs, &offset_type));
        if (is_stack_param(arg->type)) {
          new_ir_memcpy(dst, reg, type_size(arg->type));
        } else {
          new_ir_store(dst, reg);
        }
      }
    }
  }
  if (retvar_reg != NULL) {
    // gen_lval(retvar)
    VReg *dst = new_ir_bofs(retvar_reg);
    VRegType *vtype = to_vtype(ptrof(expr->type));
    new_ir_pusharg(dst);
    arg_vtypes[0] = vtype;
    ++reg_arg_count;
  }

  bool label_call = false;
  bool global = false;
  if (func->kind == EX_VAR) {
    const VarInfo *varinfo = scope_find(func->var.scope, func->var.name, NULL);
    assert(varinfo != NULL);
    label_call = varinfo->type->kind == TY_FUNC;
    global = !(varinfo->storage & VS_STATIC);
  }

  VReg *result_reg = NULL;
  {
    int vaarg_start = !functype->func.vaargs || functype->func.params == NULL ? -1 :
        functype->func.params->len + (retvar_reg != NULL ? 1 : 0);
    Type *type = expr->type;
    if (retvar_reg != NULL)
      type = ptrof(type);
    VRegType *ret_vtype = type->kind == TY_VOID ? NULL : to_vtype(type);
    if (label_call) {
      result_reg = new_ir_call(func->var.name, global, NULL, total_arg_count, reg_arg_count,
                               ret_vtype, precall, arg_vtypes, vaarg_start);
    } else {
      VReg *freg = gen_expr(func);
      result_reg = new_ir_call(NULL, false, freg, total_arg_count, reg_arg_count, ret_vtype,
                               precall, arg_vtypes, vaarg_start);
    }
  }

  return result_reg;
}

VReg *gen_arith(enum ExprKind kind, const Type *type, VReg *lhs, VReg *rhs) {
  switch (kind) {
  case EX_ADD:
  case EX_SUB:
  case EX_MUL:
#ifndef __NO_FLONUM
    if (is_flonum(type)) {
      return new_ir_bop(kind + (IR_ADD - EX_ADD), lhs, rhs, to_vtype(type));
    }
#endif
    return new_ir_bop(kind + (IR_ADD - EX_ADD), lhs, rhs, to_vtype(type));
  case EX_BITAND:
  case EX_BITOR:
  case EX_BITXOR:
  case EX_LSHIFT:
  case EX_RSHIFT:
    return new_ir_bop(kind + (IR_ADD - EX_ADD), lhs, rhs, to_vtype(type));

  case EX_DIV:
  case EX_MOD:
    assert(is_number(type));
#ifndef __NO_FLONUM
    if (is_flonum(type)) {
      return new_ir_bop(kind + (IR_DIV - EX_DIV), lhs, rhs, to_vtype(type));
    }
#endif
    return new_ir_bop(kind + (IR_DIV - EX_DIV), lhs, rhs, to_vtype(type));

  default:
    assert(false);
    return NULL;
  }
}

#ifndef __NO_FLONUM
VReg *gen_const_flonum(Expr *expr) {
  assert(expr->type->kind == TY_FLONUM);
  Initializer *init = new_initializer(IK_SINGLE, expr->token);
  init->single = expr;

  assert(curscope != NULL);
  Type *type = qualified_type(expr->type, TQ_CONST);
  const Name *name = alloc_label();
  VarInfo *varinfo = scope_add(curscope, name, type, VS_STATIC);
  VarInfo *gvarinfo = is_global_scope(curscope) ? varinfo : varinfo->static_.gvar;
  gvarinfo->global.init = init;

  VReg *src = new_ir_iofs(gvarinfo->name, false);
  return new_ir_unary(IR_LOAD, src, to_vtype(type));
}
#endif

static VReg *gen_block_expr(Stmt *stmt) {
  assert(stmt->kind == ST_BLOCK);

  if (stmt->block.scope != NULL) {
    assert(curscope == stmt->block.scope->parent);
    curscope = stmt->block.scope;
  }

  Vector *stmts = stmt->block.stmts;
  int len = stmts->len;
  VReg *result = NULL;
  if (len > 0) {
    int last = stmts->len - 1;
    for (int i = 0; i < last; ++i) {
      Stmt *stmt = stmts->data[i];
      if (stmt == NULL)
        continue;
      gen_stmt(stmt);
    }
    Stmt *last_stmt = stmts->data[last];
    if (last_stmt->kind == ST_EXPR)
      result = gen_expr(last_stmt->expr);
  }

  if (stmt->block.scope != NULL)
    curscope = curscope->parent;

  return result;
}

VReg *gen_expr(Expr *expr) {
  switch (expr->kind) {
  case EX_FIXNUM:
    {
      VReg *reg = new_const_vreg(expr->fixnum, to_vtype(expr->type));
      if (!is_im32(expr->fixnum)) {
        // Large constant value is not allowed in x86,
        // so use mov instruction.
        VReg *tmp = add_new_reg(expr->type, 0);
        new_ir_mov(tmp, reg);
        reg = tmp;
      }
      return reg;
    }
#ifndef __NO_FLONUM
  case EX_FLONUM:
    return gen_const_flonum(expr);
#endif

  case EX_STR:
    assert(!"should be handled in parser");

  case EX_VAR:
    return gen_variable(expr);

  case EX_REF:
    return gen_lval(expr->unary.sub);

  case EX_DEREF:
    {
      VReg *reg = gen_expr(expr->unary.sub);
      VReg *result;
      switch (expr->type->kind) {
      case TY_FIXNUM:
      case TY_PTR:
#ifndef __NO_FLONUM
      case TY_FLONUM:
#endif
        result = new_ir_unary(IR_LOAD, reg, to_vtype(expr->type));
        return result;

      default:
        assert(false);
        // Fallthrough to suppress compile error.
      case TY_ARRAY:
      case TY_STRUCT:
      case TY_FUNC:
        // array, struct and func values are handled as a pointer.
        return reg;
      }
    }

  case EX_MEMBER:
    {
      const MemberInfo *minfo = member_info(expr);
      if (minfo->bitfield.width > 0) {
        Expr *extract_bitfield_value(Expr *src, const MemberInfo *minfo);
        Expr *ptr = new_expr_unary(EX_REF, ptrof(minfo->type), NULL, expr);  // promote-to-int
        Expr *load = new_expr_deref(NULL, ptr);
        Expr *e = extract_bitfield_value(load, minfo);
        return gen_expr(e);
      }

      VReg *reg = gen_lval(expr);
      VReg *result;
      switch (expr->type->kind) {
      case TY_FIXNUM:
      case TY_PTR:
#ifndef __NO_FLONUM
      case TY_FLONUM:
#endif
        result = new_ir_unary(IR_LOAD, reg, to_vtype(expr->type));
        break;
      default:
        assert(false);
        // Fallthrough to suppress compile error.
      case TY_ARRAY:
      case TY_STRUCT:
        result = reg;
        break;
      }
      return result;
    }

  case EX_COMMA:
    gen_expr(expr->bop.lhs);
    return gen_expr(expr->bop.rhs);

  case EX_TERNARY:
    return gen_ternary(expr);

  case EX_CAST:
    return gen_cast(gen_expr(expr->unary.sub), expr->type);

  case EX_ASSIGN:
    {
      VReg *src = gen_expr(expr->bop.rhs);
      if (expr->bop.lhs->kind == EX_VAR) {
        Expr *lhs = expr->bop.lhs;
        switch (lhs->type->kind) {
        case TY_FIXNUM:
        case TY_PTR:
#ifndef __NO_FLONUM
        case TY_FLONUM:
#endif
          {
            Scope *scope;
            const VarInfo *varinfo = scope_find(lhs->var.scope, lhs->var.name, &scope);
            assert(varinfo != NULL);
            if (!is_global_scope(scope) && !(varinfo->storage & (VS_STATIC | VS_EXTERN))) {
              assert(varinfo->local.reg != NULL);
              new_ir_mov(varinfo->local.reg, src);
              return src;
            }
          }
          break;
        default:
          break;
        }
      }

      VReg *dst = gen_lval(expr->bop.lhs);

      switch (expr->type->kind) {
      default:
        assert(false);
        // Fallthrough to suppress compiler error.
      case TY_FIXNUM:
      case TY_PTR:
#ifndef __NO_FLONUM
      case TY_FLONUM:
#endif
        new_ir_store(dst, src);
        break;
      case TY_STRUCT:
        if (expr->type->struct_.info->size > 0) {
          new_ir_memcpy(dst, src, expr->type->struct_.info->size);
        }
        break;
      }
      return src;
    }

  case EX_PREINC:
  case EX_PREDEC:
  case EX_POSTINC:
  case EX_POSTDEC:
    {
#define IS_POST(expr)  ((expr)->kind >= EX_POSTINC)
#define IS_DEC(expr)   (((expr)->kind - EX_PREINC) & 1)
      static enum IrKind kOpAddSub[] = {IR_ADD, IR_SUB};

      Expr *target = expr->unary.sub;
      const VarInfo *varinfo = NULL;
      if (target->kind == EX_VAR && !is_global_scope(target->var.scope)) {
        const VarInfo *vi = scope_find(target->var.scope, target->var.name, NULL);
        assert(vi != NULL);
        if (!(vi->storage & (VS_STATIC | VS_EXTERN)))
          varinfo = vi;
      }

      VRegType *vtype = to_vtype(expr->type);
      VReg *before = NULL;
      VReg *lval = NULL;
      VReg *val;
      if (varinfo != NULL) {
        val = varinfo->local.reg;
        if (IS_POST(expr)) {
          before = add_new_reg(target->type, 0);
          new_ir_mov(before, val);
        }
      } else {
        lval = gen_lval(target);
        val = new_ir_unary(IR_LOAD, lval, vtype);
        if (IS_POST(expr))
          before = val;
      }

      VReg *addend =
#ifndef __NO_FLONUM
          is_flonum(target->type) ? gen_const_flonum(new_expr_flolit(target->type, NULL, 1)) :
#endif
          new_const_vreg(expr->type->kind == TY_PTR ? type_size(expr->type->pa.ptrof) : 1, vtype);
      VReg *after = new_ir_bop(kOpAddSub[IS_DEC(expr)], val, addend, vtype);
      if (varinfo != NULL)  new_ir_mov(varinfo->local.reg, after);
                      else  new_ir_store(lval, after);
      return before != NULL ? before : after;
#undef IS_POST
#undef IS_DEC
    }

  case EX_FUNCALL:
    return gen_funcall(expr);

  case EX_POS:
    return gen_expr(expr->unary.sub);

  case EX_NEG:
    {
      VReg *reg = gen_expr(expr->unary.sub);
#ifndef __NO_FLONUM
      if (is_flonum(expr->type)) {
        VReg *zero = gen_expr(new_expr_flolit(expr->type, NULL, 0.0));
        return gen_arith(EX_SUB, expr->type, zero, reg);
      }
#endif
      VReg *result = new_ir_unary(IR_NEG, reg, to_vtype(expr->type));
      return result;
    }

  case EX_BITNOT:
    {
      VReg *reg = gen_expr(expr->unary.sub);
      VReg *result = new_ir_unary(IR_BITNOT, reg, to_vtype(expr->type));
      return result;
    }

  case EX_EQ:
  case EX_NE:
  case EX_LT:
  case EX_GT:
  case EX_LE:
  case EX_GE:
    {
      enum ConditionKind cond = gen_compare_expr(expr->kind, expr->bop.lhs, expr->bop.rhs);
      switch (cond) {
      case COND_NONE:
      case COND_ANY:
        return new_const_vreg(cond == COND_ANY, to_vtype(&tyBool));
      default:
        return new_ir_cond(cond);
      }
    }

  case EX_LOGAND:
  case EX_LOGIOR:
    {
      BB *false_bb = new_bb();
      BB *next_bb = new_bb();
      gen_cond_jmp(expr, false, false_bb);
      VRegType *vtbool = to_vtype(&tyBool);
      VReg *result = add_new_reg(&tyBool, 0);
      new_ir_mov(result, new_const_vreg(true, vtbool));
      new_ir_jmp(COND_ANY, next_bb);
      set_curbb(false_bb);
      new_ir_mov(result, new_const_vreg(false, vtbool));
      set_curbb(next_bb);
      return result;
    }

  case EX_ADD:
  case EX_SUB:
  case EX_MUL:
  case EX_DIV:
  case EX_MOD:
  case EX_LSHIFT:
  case EX_RSHIFT:
  case EX_BITAND:
  case EX_BITOR:
  case EX_BITXOR:
    {
      VReg *lhs = gen_expr(expr->bop.lhs);
      VReg *rhs = gen_expr(expr->bop.rhs);
      return gen_arith(expr->kind, expr->type, lhs, rhs);
    }

  case EX_COMPLIT:
    {
      Expr *var = expr->complit.var;
      const VarInfo *varinfo = scope_find(var->var.scope, var->var.name, NULL);
      assert(varinfo != NULL);
      gen_clear_local_var(varinfo);
      gen_stmts(expr->complit.inits);
      return gen_expr(var);
    }

  case EX_BLOCK:
    return gen_block_expr(expr->block);

  default:
    fprintf(stderr, "Expr kind=%d, ", expr->kind);
    assert(!"Unhandled in gen_expr");
    break;
  }

  return NULL;
}
