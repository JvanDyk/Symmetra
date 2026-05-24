#pragma once
// SymmVM — Custom bytecode virtual machine for the Symmetra codegen.
// The codegen (AST → CUDA C++) is the most sensitive IP.
// Instead of running as native x86 instructions, it runs as bytecode
// interpreted by this simple stack-based VM.
//
// This means even if an attacker fully reverses the binary, they see:
//   - The VM interpreter (generic, doesn't reveal what it does)
//   - An encrypted bytecode blob (meaningless without the VM + key)
//
// The bytecode is also AES-256 encrypted (same key as PTX — see PtxCrypt.h).
// Decrypted bytecode lives only in a stack-allocated buffer during execution.
//
// Instruction set: 32 opcodes, 1-3 byte encoding.

#include <cstdint>
#include <string>
#include <vector>
#include <stack>

namespace symm::protect {

// ── Opcodes ───────────────────────────────────────────────────────────────────

enum class Op : uint8_t {
    // Stack ops
    PUSH_STR  = 0x01,   // PUSH_STR <len:u16> <bytes...>  — push string literal
    PUSH_INT  = 0x02,   // PUSH_INT <val:i32>
    POP       = 0x03,   // pop top
    DUP       = 0x04,   // duplicate top
    SWAP      = 0x05,   // swap top two

    // String ops (all results pushed back onto stack)
    CONCAT    = 0x10,   // concat top two strings
    APPEND    = 0x11,   // APPEND <len:u16> <bytes...>  — concat with literal
    NEWLINE   = 0x12,   // push "\n"
    INDENT    = 0x13,   // INDENT <n:u8>  — push n*4 spaces

    // Output ops
    EMIT      = 0x20,   // append top string to output buffer, pop
    EMIT_KEEP = 0x21,   // append top string to output buffer, keep

    // Integer ops
    ADD       = 0x30,
    INC       = 0x31,   // increment int on top
    INT_STR   = 0x32,   // convert int to string

    // Control flow
    CALL      = 0x40,   // CALL <addr:u32>  — call subroutine
    RET       = 0x41,   // return from subroutine
    JMP       = 0x42,   // JMP <addr:u32>
    JZ        = 0x43,   // JZ  <addr:u32>  — jump if top == 0 (and pop)
    JNZ       = 0x44,   // JNZ <addr:u32>

    // Variable ops (named slots)
    LOAD      = 0x50,   // LOAD <slot:u8>  — push variable
    STORE     = 0x51,   // STORE <slot:u8>  — pop into variable

    // Comparison
    EQ        = 0x60,   // push 1 if top two equal, 0 otherwise
    LT        = 0x61,

    HALT      = 0xFF,   // stop execution
};

// ── VM value types ────────────────────────────────────────────────────────────

struct VmVal {
    enum Kind { Str, Int } kind;
    std::string s;
    int32_t     i = 0;

    static VmVal str(std::string v)  { VmVal x; x.kind=Str; x.s=std::move(v); return x; }
    static VmVal integer(int32_t v)  { VmVal x; x.kind=Int; x.i=v; return x; }
};

// ── VM state ──────────────────────────────────────────────────────────────────

struct SymmVm {
    std::stack<VmVal>        stack;
    std::vector<VmVal>       slots;     // named variable slots (max 64)
    std::string              output;    // accumulated output (the generated CUDA C++)
    std::vector<uint32_t>    callStack; // return addresses

    SymmVm() : slots(64) {}

    // Execute bytecode. Returns output (generated CUDA C++).
    std::string run(const uint8_t* bytecode, size_t len);

    // Assemble a human-readable .symm bytecode listing into binary
    static std::vector<uint8_t> assemble(const std::string& source);

private:
    const uint8_t* _bc  = nullptr;
    size_t         _len = 0;
    uint32_t       _pc  = 0;

    uint8_t  readU8()  { return _bc[_pc++]; }
    uint16_t readU16() { uint16_t v = (_bc[_pc]<<8)|_bc[_pc+1]; _pc+=2; return v; }
    uint32_t readU32() { uint32_t v = ((uint32_t)_bc[_pc]<<24)|((uint32_t)_bc[_pc+1]<<16)|
                                      ((uint32_t)_bc[_pc+2]<<8)|_bc[_pc+3]; _pc+=4; return v; }
    int32_t  readI32() { return (int32_t)readU32(); }

    VmVal& top()   { return stack.top(); }
    VmVal  pop()   { auto v = stack.top(); stack.pop(); return v; }
    void   push(VmVal v) { stack.push(std::move(v)); }
};

} // namespace symm::protect
