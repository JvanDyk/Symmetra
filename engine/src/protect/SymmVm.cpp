#include "SymmVm.h"
#include <stdexcept>
#include <sstream>

namespace symm::protect {

std::string SymmVm::run(const uint8_t* bytecode, size_t len) {
    _bc  = bytecode;
    _len = len;
    _pc  = 0;
    output.clear();
    while (!stack.empty()) stack.pop();

    while (_pc < _len) {
        Op op = static_cast<Op>(readU8());

        switch (op) {

        // ── Stack ops ─────────────────────────────────────────────────────────
        case Op::PUSH_STR: {
            uint16_t slen = readU16();
            std::string s(reinterpret_cast<const char*>(_bc + _pc), slen);
            _pc += slen;
            push(VmVal::str(std::move(s)));
            break;
        }
        case Op::PUSH_INT:
            push(VmVal::integer(readI32()));
            break;
        case Op::POP:
            pop();
            break;
        case Op::DUP:
            push(top());
            break;
        case Op::SWAP: {
            auto a = pop(), b = pop();
            push(a); push(b);
            break;
        }

        // ── String ops ────────────────────────────────────────────────────────
        case Op::CONCAT: {
            auto b = pop(), a = pop();
            push(VmVal::str(a.s + b.s));
            break;
        }
        case Op::APPEND: {
            uint16_t slen = readU16();
            std::string lit(reinterpret_cast<const char*>(_bc + _pc), slen);
            _pc += slen;
            auto a = pop();
            push(VmVal::str(a.s + lit));
            break;
        }
        case Op::NEWLINE:
            push(VmVal::str("\n"));
            break;
        case Op::INDENT: {
            uint8_t n = readU8();
            push(VmVal::str(std::string(n * 4, ' ')));
            break;
        }

        // ── Output ops ────────────────────────────────────────────────────────
        case Op::EMIT: {
            auto v = pop();
            output += (v.kind == VmVal::Str) ? v.s : std::to_string(v.i);
            break;
        }
        case Op::EMIT_KEEP: {
            auto& v = top();
            output += (v.kind == VmVal::Str) ? v.s : std::to_string(v.i);
            break;
        }

        // ── Integer ops ───────────────────────────────────────────────────────
        case Op::ADD: {
            auto b = pop(), a = pop();
            push(VmVal::integer(a.i + b.i));
            break;
        }
        case Op::INC: {
            auto& v = top();
            v.i++;
            break;
        }
        case Op::INT_STR: {
            auto v = pop();
            push(VmVal::str(std::to_string(v.i)));
            break;
        }

        // ── Variable ops ──────────────────────────────────────────────────────
        case Op::LOAD: {
            uint8_t slot = readU8();
            push(slots[slot]);
            break;
        }
        case Op::STORE: {
            uint8_t slot = readU8();
            slots[slot] = pop();
            break;
        }

        // ── Control flow ──────────────────────────────────────────────────────
        case Op::CALL: {
            uint32_t addr = readU32();
            callStack.push_back(_pc);
            _pc = addr;
            break;
        }
        case Op::RET:
            if (callStack.empty()) goto halt;
            _pc = callStack.back();
            callStack.pop_back();
            break;
        case Op::JMP:
            _pc = readU32();
            break;
        case Op::JZ: {
            uint32_t addr = readU32();
            auto v = pop();
            if ((v.kind == VmVal::Int && v.i == 0) ||
                (v.kind == VmVal::Str && v.s.empty()))
                _pc = addr;
            break;
        }
        case Op::JNZ: {
            uint32_t addr = readU32();
            auto v = pop();
            if (!((v.kind == VmVal::Int && v.i == 0) ||
                  (v.kind == VmVal::Str && v.s.empty())))
                _pc = addr;
            break;
        }

        // ── Comparison ────────────────────────────────────────────────────────
        case Op::EQ: {
            auto b = pop(), a = pop();
            bool eq = (a.kind == b.kind)
                && (a.kind == VmVal::Str ? a.s == b.s : a.i == b.i);
            push(VmVal::integer(eq ? 1 : 0));
            break;
        }
        case Op::LT: {
            auto b = pop(), a = pop();
            push(VmVal::integer(a.i < b.i ? 1 : 0));
            break;
        }

        case Op::HALT:
        default:
            goto halt;
        }
    }
halt:
    return output;
}

} // namespace symm::protect
