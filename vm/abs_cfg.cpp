/*
 * Copyright 2018 VMware, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>
#include <assert.h>

#include <vector>
#include <string>

#include <boost/optional.hpp>

#include "abs_common.hpp"
#include "abs_cst_regs.hpp"
#include "abs_cfg.hpp"
#include "abs_state.h"

using boost::optional;
using std::to_string;

static auto get_jump(struct ebpf_inst inst, uint16_t pc) -> optional<uint16_t>
{
    if ((inst.opcode & EBPF_CLS_MASK) == EBPF_CLS_JMP
            && inst.opcode != EBPF_OP_CALL
            && inst.opcode != EBPF_OP_EXIT) {
        return pc + 1 + inst.offset;
    }
    return boost::none;
}

static auto get_fallthrough(struct ebpf_inst inst, uint16_t pc) -> optional<uint16_t>
{
    if (inst.opcode != EBPF_OP_JA && inst.opcode != EBPF_OP_EXIT) {
        /* eBPF has one 16-byte instruction: BPF_LD | BPF_DW | BPF_IMM which consists
        of two consecutive 'struct ebpf_inst' 8-byte blocks and interpreted as single
        instruction that loads 64-bit immediate value into a dst_reg. */
        if (inst.opcode == EBPF_OP_LDDW) {
            pc++;
        }
        return pc + 1;
    }
    return boost::none;
}
static auto build_jmp(cfg_t& cfg, uint16_t pc, uint16_t target) -> basic_block_t&
{
    basic_block_t& assumption = cfg.insert(label(pc, target));
    cfg.get_node(label(pc)) >> assumption;
    assumption >> cfg.insert(label(target));
    return assumption;
}

static void link(cfg_t& cfg, uint16_t pc, uint16_t target)
{
    cfg.get_node(label(pc)) >> cfg.insert(label(target));
}

void build_cfg(cfg_t& cfg, std::vector<ebpf_inst> insts)
{
    cst_regs regs;

    for (uint16_t pc = 0; pc < insts.size(); pc++) {
        auto inst = insts[pc];

        regs.exec(inst, cfg.insert(label(pc)));

        if (inst.opcode == EBPF_OP_EXIT) {
            cfg.set_exit(label(pc));
        }

        optional<uint16_t> jmp_target = get_jump(insts[pc], pc);
        optional<uint16_t> fall_target = get_fallthrough(insts[pc], pc);
        if (jmp_target) {
            if (inst.opcode != EBPF_OP_JA)  {
                auto& assumption = build_jmp(cfg, pc, *jmp_target);
                regs.jump(inst, assumption, true);
            } else {
                link(cfg, pc, *jmp_target);
            }
        }
        
        if (fall_target) {
            if (jmp_target) {
                auto& assumption = build_jmp(cfg, pc, *fall_target);
                regs.jump(inst, assumption, false);
            } else {
                link(cfg, pc, *fall_target);
            }
            // skip imm of lddw
            pc = *fall_target - 1;
        }
    }
    cfg.simplify();
}

