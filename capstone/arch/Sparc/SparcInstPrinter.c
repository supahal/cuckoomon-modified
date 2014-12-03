//===-- SparcInstPrinter.cpp - Convert Sparc MCInst to assembly syntax --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class prints an Sparc MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

/* Capstone Disassembly Engine */
/* By Nguyen Anh Quynh <aquynh@gmail.com>, 2013-2014 */

#ifdef CAPSTONE_HAS_SPARC

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SparcInstPrinter.h"
#include "../../MCInst.h"
#include "../../utils.h"
#include "../../SStream.h"
#include "../../MCRegisterInfo.h"
#include "../../MathExtras.h"
#include "SparcMapping.h"

#include "Sparc.h"

static char *getRegisterName(unsigned RegNo);
static void printInstruction(MCInst *MI, SStream *O, MCRegisterInfo *MRI);
static void printMemOperand(MCInst *MI, int opNum, SStream *O, const char *Modifier);
static void printOperand(MCInst *MI, int opNum, SStream *O);

static void Sparc_add_hint(MCInst *MI, unsigned int hint)
{
	if (MI->csh->detail) {
		MI->flat_insn->detail->sparc.hint = hint;
	}
}

static void Sparc_add_reg(MCInst *MI, unsigned int reg)
{
	if (MI->csh->detail) {
		MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].type = SPARC_OP_REG;
		MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].reg = reg;
		MI->flat_insn->detail->sparc.op_count++;
	}
}

static void set_mem_access(MCInst *MI, bool status)
{
	if (MI->csh->detail != CS_OPT_ON)
		return;

	MI->csh->doing_mem = status;

	if (status) {
		MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].type = SPARC_OP_MEM;
		MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].mem.base = SPARC_REG_INVALID;
		MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].mem.disp = 0;
	} else {
		// done, create the next operand slot
		MI->flat_insn->detail->sparc.op_count++;
	}
}

void Sparc_post_printer(csh ud, cs_insn *insn, char *insn_asm, MCInst *mci)
{
	if (((cs_struct *)ud)->detail != CS_OPT_ON)
		return;

	// fix up some instructions
	if (insn->id == SPARC_INS_CASX) {
		// first op is actually a memop, not regop
		insn->detail->sparc.operands[0].type = SPARC_OP_MEM;
		insn->detail->sparc.operands[0].mem.base = insn->detail->sparc.operands[0].reg;
		insn->detail->sparc.operands[0].mem.disp = 0;
	}
}

static void printRegName(SStream *OS, unsigned RegNo)
{
	SStream_concat0(OS, "%");
	SStream_concat0(OS, getRegisterName(RegNo));
}

#define GET_INSTRINFO_ENUM
#include "SparcGenInstrInfo.inc"

#define GET_REGINFO_ENUM
#include "SparcGenRegisterInfo.inc"

static bool printSparcAliasInstr(MCInst *MI, SStream *O)
{
	switch (MCInst_getOpcode(MI)) {
		default: return false;
		case SP_JMPLrr:
		case SP_JMPLri:
				 if (MCInst_getNumOperands(MI) != 3)
					 return false;
				 if (!MCOperand_isReg(MCInst_getOperand(MI, 0)))
					 return false;

				 switch (MCOperand_getReg(MCInst_getOperand(MI, 0))) {
					 default: return false;
					 case SP_G0: // jmp $addr | ret | retl
							  if (MCOperand_isImm(MCInst_getOperand(MI, 2)) &&
									MCOperand_getImm(MCInst_getOperand(MI, 2)) == 8) {
								  switch(MCOperand_getReg(MCInst_getOperand(MI, 1))) {
									  default: break;
									  case SP_I7: SStream_concat0(O, "ret"); MCInst_setOpcodePub(MI, SPARC_INS_RET); return true;
									  case SP_O7: SStream_concat0(O, "retl"); MCInst_setOpcodePub(MI, SPARC_INS_RETL); return true;
								  }
							  }

							  SStream_concat0(O, "jmp\t");
							  MCInst_setOpcodePub(MI, SPARC_INS_JMP);
							  printMemOperand(MI, 1, O, NULL);
							  return true;
					 case SP_O7: // call $addr
							  SStream_concat0(O, "call ");
							  MCInst_setOpcodePub(MI, SPARC_INS_CALL);
							  printMemOperand(MI, 1, O, NULL);
							  return true;
				 }
		case SP_V9FCMPS:
		case SP_V9FCMPD:
		case SP_V9FCMPQ:
		case SP_V9FCMPES:
		case SP_V9FCMPED:
		case SP_V9FCMPEQ:
				 if (MI->csh->mode & CS_MODE_V9 || (MCInst_getNumOperands(MI) != 3) ||
						 (!MCOperand_isReg(MCInst_getOperand(MI, 0))) ||
						 (MCOperand_getReg(MCInst_getOperand(MI, 0)) != SP_FCC0))
						 return false;
				 // if V8, skip printing %fcc0.
				 switch(MCInst_getOpcode(MI)) {
					 default:
					 case SP_V9FCMPS:  SStream_concat0(O, "fcmps\t"); MCInst_setOpcodePub(MI, SPARC_INS_FCMPS); break;
					 case SP_V9FCMPD:  SStream_concat0(O, "fcmpd\t"); MCInst_setOpcodePub(MI, SPARC_INS_FCMPD); break;
					 case SP_V9FCMPQ:  SStream_concat0(O, "fcmpq\t"); MCInst_setOpcodePub(MI, SPARC_INS_FCMPQ); break;
					 case SP_V9FCMPES: SStream_concat0(O, "fcmpes\t"); MCInst_setOpcodePub(MI, SPARC_INS_FCMPES); break;
					 case SP_V9FCMPED: SStream_concat0(O, "fcmped\t"); MCInst_setOpcodePub(MI, SPARC_INS_FCMPED); break;
					 case SP_V9FCMPEQ: SStream_concat0(O, "fcmpeq\t"); MCInst_setOpcodePub(MI, SPARC_INS_FCMPEQ); break;
				 }
				 printOperand(MI, 1, O);
				 SStream_concat0(O, ", ");
				 printOperand(MI, 2, O);
				 return true;
	}
}

static void printOperand(MCInst *MI, int opNum, SStream *O)
{
	int Imm;
	unsigned reg;
	MCOperand *MO = MCInst_getOperand(MI, opNum);

	if (MCOperand_isReg(MO)) {
		reg = MCOperand_getReg(MO);
		printRegName(O, reg);
		reg = Sparc_map_register(reg);

		if (MI->csh->detail) {
			if (MI->csh->doing_mem) {
				if (MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].mem.base)
					MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].mem.index = reg;
				else
					MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].mem.base = reg;
			} else {
				MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].type = SPARC_OP_REG;
				MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].reg = reg;
				MI->flat_insn->detail->sparc.op_count++;
			}
		}

		return;
	}

	if (MCOperand_isImm(MO)) {
		Imm = (int)MCOperand_getImm(MO);

		// get absolute address for CALL/Bxx
		if (MI->Opcode == SP_CALL)
			Imm += MI->address;
		else if (MI->flat_insn->id == SPARC_INS_B)
			// pc + (disp30 * 4)
			Imm = MI->address + Imm * 4;

		if (Imm >= 0) {
			if (Imm > HEX_THRESHOLD)
				SStream_concat(O, "0x%x", Imm);
			else
				SStream_concat(O, "%u", Imm);
		} else {
			if (Imm < -HEX_THRESHOLD)
				SStream_concat(O, "-0x%x", -Imm);
			else
				SStream_concat(O, "-%u", -Imm);
		}

		if (MI->csh->detail) {
			if (MI->csh->doing_mem) {
				MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].mem.disp = Imm;
			} else {
				MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].type = SPARC_OP_IMM;
				MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].imm = Imm;
				MI->flat_insn->detail->sparc.op_count++;
			}
		}
	}

	return;
}

static void printMemOperand(MCInst *MI, int opNum, SStream *O, const char *Modifier)
{
	MCOperand *MO;

	set_mem_access(MI, true);
	printOperand(MI, opNum, O);

	// If this is an ADD operand, emit it like normal operands.
	if (Modifier && !strcmp(Modifier, "arith")) {
		SStream_concat0(O, ", ");
		printOperand(MI, opNum + 1, O);
		set_mem_access(MI, false);
		return;
	}

	MO = MCInst_getOperand(MI, opNum + 1);

	if (MCOperand_isReg(MO) && (MCOperand_getReg(MO) == SP_G0)) {
		set_mem_access(MI, false);
		return;   // don't print "+%g0"
	}

	if (MCOperand_isImm(MO) && (MCOperand_getImm(MO) == 0)) {
		set_mem_access(MI, false);
		return;   // don't print "+0"
	}

	SStream_concat0(O, "+");	// qq

	printOperand(MI, opNum + 1, O);
	set_mem_access(MI, false);
}

static void printCCOperand(MCInst *MI, int opNum, SStream *O)
{
	int CC = (int)MCOperand_getImm(MCInst_getOperand(MI, opNum)) + 256;

	switch (MCInst_getOpcode(MI)) {
		default: break;
		case SP_FBCOND:
		case SP_FBCONDA:
		case SP_BPFCC:
		case SP_BPFCCA:
		case SP_BPFCCNT:
		case SP_BPFCCANT:
		case SP_MOVFCCrr:  case SP_V9MOVFCCrr:
		case SP_MOVFCCri:  case SP_V9MOVFCCri:
		case SP_FMOVS_FCC: case SP_V9FMOVS_FCC:
		case SP_FMOVD_FCC: case SP_V9FMOVD_FCC:
		case SP_FMOVQ_FCC: case SP_V9FMOVQ_FCC:
				 // Make sure CC is a fp conditional flag.
				 CC = (CC < 16+256) ? (CC + 16) : CC;
				 break;
	}

	SStream_concat0(O, SPARCCondCodeToString((sparc_cc)CC));

	if (MI->csh->detail)
		MI->flat_insn->detail->sparc.cc = (sparc_cc)CC;
}


static bool printGetPCX(MCInst *MI, unsigned opNum, SStream *O)
{
	return true;
}


#define PRINT_ALIAS_INSTR
#include "SparcGenAsmWriter.inc"

void Sparc_printInst(MCInst *MI, SStream *O, void *Info)
{
	char *mnem, *p;
	char instr[64];	// Sparc has no instruction this long

	mnem = printAliasInstr(MI, O, Info);
	if (mnem) {
		// fixup instruction id due to the change in alias instruction
		strncpy(instr, mnem, strlen(mnem));
		instr[strlen(mnem)] = '\0';
		// does this contains hint with a coma?
		p = strchr(instr, ',');
		if (p)
			*p = '\0';	// now instr only has instruction mnemonic
		MCInst_setOpcodePub(MI, Sparc_map_insn(instr));
		switch(MCInst_getOpcode(MI)) {
			case SP_BCOND:
			case SP_BCONDA:
			case SP_BPICCANT:
			case SP_BPICCNT:
			case SP_BPXCCANT:
			case SP_BPXCCNT:
			case SP_TXCCri:
			case SP_TXCCrr:
				if (MI->csh->detail) {
					// skip 'b', 't'
					MI->flat_insn->detail->sparc.cc = Sparc_map_ICC(instr + 1);
					MI->flat_insn->detail->sparc.hint = Sparc_map_hint(mnem);
				}
				break;
			case SP_BPFCCANT:
			case SP_BPFCCNT:
				if (MI->csh->detail) {
					// skip 'fb'
					MI->flat_insn->detail->sparc.cc = Sparc_map_FCC(instr + 2);
					MI->flat_insn->detail->sparc.hint = Sparc_map_hint(mnem);
				}
				break;
			case SP_FMOVD_ICC:
			case SP_FMOVD_XCC:
			case SP_FMOVQ_ICC:
			case SP_FMOVQ_XCC:
			case SP_FMOVS_ICC:
			case SP_FMOVS_XCC:
				if (MI->csh->detail) {
					// skip 'fmovd', 'fmovq', 'fmovs'
					MI->flat_insn->detail->sparc.cc = Sparc_map_ICC(instr + 5);
					MI->flat_insn->detail->sparc.hint = Sparc_map_hint(mnem);
				}
				break;
			case SP_MOVICCri:
			case SP_MOVICCrr:
			case SP_MOVXCCri:
			case SP_MOVXCCrr:
				if (MI->csh->detail) {
					// skip 'mov'
					MI->flat_insn->detail->sparc.cc = Sparc_map_ICC(instr + 3);
					MI->flat_insn->detail->sparc.hint = Sparc_map_hint(mnem);
				}
				break;
			case SP_V9FMOVD_FCC:
			case SP_V9FMOVQ_FCC:
			case SP_V9FMOVS_FCC:
				if (MI->csh->detail) {
					// skip 'fmovd', 'fmovq', 'fmovs'
					MI->flat_insn->detail->sparc.cc = Sparc_map_FCC(instr + 5);
					MI->flat_insn->detail->sparc.hint = Sparc_map_hint(mnem);
				}
				break;
			case SP_V9MOVFCCri:
			case SP_V9MOVFCCrr:
				if (MI->csh->detail) {
					// skip 'mov'
					MI->flat_insn->detail->sparc.cc = Sparc_map_FCC(instr + 3);
					MI->flat_insn->detail->sparc.hint = Sparc_map_hint(mnem);
				}
				break;
			default:
				break;
		}
		cs_mem_free(mnem);
	} else {
		if (!printSparcAliasInstr(MI, O))
			printInstruction(MI, O, NULL);
	}
}

void Sparc_addReg(MCInst *MI, int reg)
{
	if (MI->csh->detail) {
		MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].type = SPARC_OP_REG;
		MI->flat_insn->detail->sparc.operands[MI->flat_insn->detail->sparc.op_count].reg = reg;
		MI->flat_insn->detail->sparc.op_count++;
	}
}

#endif