#include "rabbitizer.hpp"
#include "fmt/format.h"
#include "fmt/ostream.h"

int main() {
	//rabbitizer::InstructionRsp instr{ 0xE9DD3801, 0x040013E0 }; // suv $v29[0], 0x8($14)
	rabbitizer::InstructionRsp instr{ 0xEAF70B84, 0x04001624 }; // ssv $v23[7], 0x8($23)
	//rabbitizer::InstructionRsp instr{ 0x4B5E888F, 0x04001414 }; // vmadh $v2, $v17, $v30[2]
	bool has_element = false;
	int element = 0;

	fmt::print("{}\n", instr.disassemble(0));
	fmt::print("{}\n", instr.getOpcodeName());
	fmt::print("{}\n", instr.disassembleOperands());

	if (instr.hasOperand(rabbitizer::OperandType::rsp_vt_elementhigh)) {
		element = instr.GetRsp_elementhigh();
		has_element = true;
	} else if (instr.hasOperand(rabbitizer::OperandType::rsp_vt_elementlow)) {
		if (has_element) {
			fmt::print(stderr, "Instruction cannot have two element values {}\n", instr.disassemble(0));
			std::exit(EXIT_FAILURE);
		}
		element = instr.GetRsp_elementlow();
		has_element = true;
	}

	if (has_element) {
		fmt::print("element: 0x{:X}\n", element);
	}

	return 0;
}
