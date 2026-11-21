# Copyright (c) 2021-2026 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Assembler specific extension of ISAPI
Instruction.class_eval do
  def asm_token
    mnemonic.tr('.', '_').upcase
  end

  def call?
    properties.include?('call')
  end

  def range?
    mnemonic.split('.')[-1] == 'range'
  end

  def simple_call?
    call? && !range? && !properties.include?('dynamic')
  end

  def return?
    stripped_mnemonic == 'return'
  end

  def return_obj?
    mnemonic == 'return.obj'
  end

  def return64?
    mnemonic == 'return.64'
  end

  def return32?
    mnemonic == 'return'
  end

  def return_void?
    mnemonic == 'return.void'
  end
end

def bit_cast(what, to_type, from_type)
  "bit_cast<#{to_type}, #{from_type}>(static_cast<#{from_type}>(std::get<double>(#{what})))"
end

def index_of_max(a)
  a.each_with_index.max[1] # returns index of `last` max value
end

def max_number_of_src_regs
  Panda::instructions.map do |insn|
    insn.operands.select(&:reg?).select(&:src?).size
  end.max
end

IR = Struct.new(:opcode, :flags, :dst_idx, :use_idxs)

module Panda
  def self.pseudo_instructions
    insns = []
    insns << IR.new('MOVX',       ['InstFlags::PSEUDO'], 0, [1])
    insns << IR.new('LDAX',       ['InstFlags::PSEUDO', 'InstFlags::ACC_WRITE'], 'INVALID_REG_IDX', [0])
    insns << IR.new('STAX',       ['InstFlags::PSEUDO', 'InstFlags::ACC_READ'], 0, [])
    insns << IR.new('NEWX',       ['InstFlags::PSEUDO'], 0, [])
    insns << IR.new('INITOBJX',   ['InstFlags::PSEUDO', 'InstFlags::CALL', 'InstFlags::ACC_WRITE'], 'INVALID_REG_IDX', [])
    insns << IR.new('CALLX',      ['InstFlags::PSEUDO', 'InstFlags::CALL', 'InstFlags::ACC_WRITE'], 'INVALID_REG_IDX', [])
    insns << IR.new('CALLX_VIRT', ['InstFlags::PSEUDO', 'InstFlags::CALL', 'InstFlags::ACC_WRITE'], 'INVALID_REG_IDX', [])
    insns << IR.new('B_P_CALLIX', ['InstFlags::PSEUDO', 'InstFlags::CALL', 'InstFlags::ACC_WRITE'], 'INVALID_REG_IDX', [])
    insns << IR.new('B_P_CALLIEX',['InstFlags::PSEUDO', 'InstFlags::CALL', 'InstFlags::ACC_WRITE'], 'INVALID_REG_IDX', [])
    insns
  end
end

# returns array of OpenStruct with fields
# name - name of variable in emitter code
# type - type of variable in emitter code
def assembler_signature(group, is_jump)
  insn = group.first
  format_ops(insn.format).each do |o|
    if o.name.start_with?('imm')
      if insn.asm_token.start_with?('F')
        o.type, o.name = is_jump ? ['const std::string &', 'label'] : ['double', o.name]
      else
        o.type, o.name = is_jump ? ['const std::string &', 'label'] : ['int64_t', o.name]
      end
    elsif o.name.start_with?('id')
      o.type, o.name = ['const std::string &', o.name]
    else
      o.type = 'uint16_t'
    end
  end
end

# ---------------------------------------------------------------------------
# Per-(mnemonic, format) subclass support.
#
# The pandasm::Ins hierarchy used to generate a single subclass per mnemonic,
# with every register member typed as uint16_t (the widest possible) and every
# immediate member typed as int64_t/double (the widest possible). That wastes
# memory: an instruction whose format only needs an 8-bit register/immediate
# still paid for 16/64 bits of storage.
#
# To save object space we now generate one subclass per (mnemonic, format)
# pair, where each member is typed with the narrowest C++ integral type that
# can hold the operand's encoding width for that specific format. The factory
# Ins::CreateIns(opcode, regs, imms, ids) picks the smallest format whose
# widths accommodate the provided values, so the chosen subclass stores every
# operand without truncation while using as few bytes as possible.
#
# Helpers below are shared by assembly-ins.h.erb, ins_create_api.h.erb and the
# Ins::CreateIns factory.
# ---------------------------------------------------------------------------

# Per-(mnemonic, format) C++ subclass name. The mnemonic part mirrors the
# historical insn2node naming (so a single-format instruction keeps its
# familiar name); multi-format instructions get a format-derived suffix.
def insn_class_name(insn)
  base = insn.mnemonic.split('.').map { |p| p == '64' ? 'Wide' : p.capitalize }.join
  pretty = insn.format.pretty
  return base if pretty == 'none'
  suffix = pretty.split('_').map(&:capitalize).join
  "#{base}_#{suffix}"
end

# Narrowest C++ storage type for an operand, derived from the operand's
# encoding width in the current format and the signedness/float-ness declared
# by the sig type. Registers use uint8_t for 4-/8-bit formats and uint16_t for
# 16-bit formats; imms use the matching int*_t/uint*_t/double; ids and jump
# labels use std::string.
def operand_storage_type(op, insn)
  if op.reg?
    return op.width <= 8 ? 'uint8_t' : 'uint16_t'
  end
  if op.imm?
    return 'std::string' if insn.jump?
    return 'double' if op.is_float_imm?
    w = op.width
    if op.is_unsigned_imm?
      return w <= 8 ? 'uint8_t' : (w <= 16 ? 'uint16_t' : (w <= 32 ? 'uint32_t' : 'uint64_t'))
    end
    if op.is_signed_imm?
      return w <= 8 ? 'int8_t' : (w <= 16 ? 'int16_t' : (w <= 32 ? 'int32_t' : 'int64_t'))
    end
    return 'int64_t'
  end
  return 'std::string' if op.id?
  raise "Unexpected operand #{op.name}"
end

# Alignment (in bytes) of the C++ storage type used in a subclass member
# declaration string (e.g. "uint8_t reg0_"). Used to sort member declarations
# by decreasing alignment so the compiler inserts the least amount of padding,
# shrinking the per-instruction object footprint.
def member_alignment(member_decl)
  case member_decl
  when /std::string\*/, /double/, /int64_t/, /uint64_t/
    8
  when /int32_t/, /uint32_t/
    4
  when /int16_t/, /uint16_t/
    2
  when /int8_t/, /uint8_t/
    1
  else
    8
  end
end

# Builds the member/ctor/init metadata for a per-format subclass.
# Returns a hash with:
#   :members      -> array of "type name_" member declarations (sorted by
#                    decreasing alignment to minimise padding)
#   :ctor_args    -> array of "type name" typed-ctor parameters (sig order)
#   :reg_info     -> array of [name, type] for register operands
#   :imm_info     -> array of [name, type] for immediate operands
#   :id_info     -> array of [name, type] for id operands (incl. jump labels)
#   :init         -> vector-ctor initialization statements (with proper casts)
#   :regs_init    -> comma-joined member names for Regs() return
#   :imms_init    -> comma-joined member names for Imms() return
#   :ids_init     -> comma-joined member names for Ids() return
#   :reg_count/:imm_count/:id_count -> operand counts per category
def insn_ctor_info(insn)
  sig = insn.sig
  ops_str = sig.include?(' ') ? sig.match(/(\S+) (.+)/).captures[1] : ''
  operands = ops_str.split(', ')
  fmt_ops = insn.operands
  members = []
  ctor_args = []
  reg_info = []; imm_info = []; id_info = []
  reg_init = []; imm_init = []; id_init = []
  reg_idx = 0; imm_idx = 0; id_idx = 0
  id_count = 0
  operands.each_with_index do |operand, i|
    name = operand.split(':')[0]
    op = fmt_ops[i]
    type = operand_storage_type(op, insn)
    if op.reg?
      reg_info << [name, type]
      members << "#{type} #{name}_"
      ctor_args << "#{type} #{name}"
      reg_init << "#{name}_ = regs[#{reg_idx}];"
      reg_idx += 1
    elsif op.imm? && insn.jump?
      # Jump immediate is a label stored as a string id.
      mname = "stringId_#{id_count}"
      id_count += 1
      id_info << [mname, 'const std::string*']
      members << "const std::string* #{mname}_"
      ctor_args << "std::string #{mname}"
      id_init << "#{mname}_ = ids[#{id_idx}];"
      id_idx += 1
    elsif op.imm?
      imm_info << [name, type]
      members << "#{type} #{name}_"
      ctor_args << "#{type} #{name}"
      if type == 'double'
        imm_init << "#{name}_ = std::holds_alternative<double>(imms[#{imm_idx}]) ? " \
                    "std::get<double>(imms[#{imm_idx}]) : " \
                    "static_cast<double>(std::get<int64_t>(imms[#{imm_idx}]));"
      else
        imm_init << "#{name}_ = std::holds_alternative<int64_t>(imms[#{imm_idx}]) ? " \
                    "static_cast<#{type}>(std::get<int64_t>(imms[#{imm_idx}])) : " \
                    "static_cast<#{type}>(std::get<double>(imms[#{imm_idx}]));"
      end
      imm_idx += 1
    elsif op.id?
      mname = "stringId_#{id_count}"
      id_count += 1
      id_info << [mname, 'const std::string*']
      members << "const std::string* #{mname}_"
      ctor_args << "std::string #{mname}"
      id_init << "#{mname}_ = ids[#{id_idx}];"
      id_idx += 1
    else
      raise "Unexpected operand #{op.name}"
    end
  end
  members.sort_by! { |m| -member_alignment(m) }
  {
    members: members, ctor_args: ctor_args,
    reg_info: reg_info, imm_info: imm_info, id_info: id_info,
    init: (id_init + imm_init + reg_init).join("\n        "),
    regs_init: reg_info.map { |n, _| "#{n}_" }.join(', '),
    imms_init: imm_info.map { |n, t| t == 'double' ? "IType(#{n}_)" : "IType(int64_t(#{n}_))" }.join(', '),
    ids_init: id_info.map { |n, _| "*#{n}_" }.join(', '),
    reg_count: reg_idx, imm_count: imm_idx, id_count: id_idx
  }
end

# C++ boolean expression (referencing regs/imms/ids) that is true iff the
# provided operand values fit the encoding widths of the given instruction's
# format. Used by Ins::CreateIns to pick the smallest fitting subclass.
def format_fit_condition(insn)
  conds = []
  reg_idx = 0; imm_idx = 0; id_idx = 0
  insn.operands.each do |op|
    if op.reg?
      conds << "Ins::FitsReg(regs[#{reg_idx}], #{op.width})"
      reg_idx += 1
    elsif op.imm?
      if insn.jump?
        id_idx += 1 # label is stored as id; no width constraint at creation
      elsif op.is_float_imm?
        imm_idx += 1 # f64 always fits the 64-bit double storage
      elsif op.is_unsigned_imm?
        conds << "(std::holds_alternative<int64_t>(imms[#{imm_idx}]) && " \
                 "Ins::FitsUInt(std::get<int64_t>(imms[#{imm_idx}]), #{op.width}))"
        imm_idx += 1
      elsif op.is_signed_imm?
        conds << "(std::holds_alternative<int64_t>(imms[#{imm_idx}]) && " \
                 "Ins::FitsInt(std::get<int64_t>(imms[#{imm_idx}]), #{op.width}))"
        imm_idx += 1
      else
        # Neither signed nor unsigned nor float: the fit chain below would silently
        # skip the operand and misalign every subsequent imm index. Fail at
        # generation time instead of emitting a wrong resolver chain.
        raise "imm operand '#{op.name}' of '#{insn.mnemonic}' has no i/u/f width prefix"
      end
    elsif op.id?
      id_idx += 1
    end
  end
  conds.unshift("regs.size() > #{reg_idx - 1}") if reg_idx > 0
  conds.unshift("imms.size() > #{imm_idx - 1}") if imm_idx > 0
  conds.unshift("ids.size() > #{id_idx - 1}") if id_idx > 0
  conds.empty? ? 'true' : conds.join(' && ')
end
