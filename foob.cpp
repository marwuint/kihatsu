

#include <array>
#include <bitset>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

volatile int pc = 0;
// volatile uint64_t rram = 0;
register long rram __asm("r14");
// volatile uint64_t erram = 0;
register long erram __asm("r13");
uint16_t *pm;
enum operacija { copy_ram = 0, copy_pm = 1, nand = 2, xor_ = 3, clr = 4, comp = 5, nop = 6, eclr = 11, ecomp = 12 };
char *operacija_str[] = {"copyRAM", "copyPM", "nand", "xor", "clr", "comp", "nop"};

struct Op {
  int a;
  int b;
  operacija type;
  int ext;
  bool cp;
  uint16_t raw;
};

int peek(int a) {
  volatile int res = 0;
  if (a < 64)
    asm("btq %1, %2" : "=@ccc"(res) : "IOr"((uint64_t)(a)), "r"(rram));
  else
    asm("btq %1, %2" : "=@ccc"(res) : "IOr"((uint64_t)(a - 64)), "r"(erram));
  return res;
}
int poke(int a, bool v) {
  volatile int res = 0;
  if (a < 64) {
    if (v) rram |= (1LL << a);
    // rram &= ~(1LL << a);
    // rram |= (v ? 1LL : 0) << a;
    // asm("btsq %1, %2" : "=@ccc"(res) : "IOr"((uint64_t)(a)), "m"(rram));
    else
      rram &= ~(1LL << a);
    // asm("btrq %1, %2" : "=@ccc"(res) : "IOr"((uint64_t)(a)), "m"(rram));
  } else {
    if (v)
      asm("btsq %1, %2" : "=r"(erram) : "IOr"((uint64_t)(a - 64)), "r"(erram));
    else
      asm("btrq %1, %2" : "=r"(erram) : "IOr"((uint64_t)(a - 64)), "r"(erram));
  }
  return res;
}
void rnand(int a, int b) {
  auto x = peek(a);
  auto y = peek(b);
  poke(a, !(x && y));
}
void rxor(int a, int b) {
  // if (a < 64) {
  //   if (b < 64)
  //     rram ^= (((rram & (1LL << b)) >> b) << a);
  //   else
  //     rram ^= (((erram & (1LL << (b - 64)) >> (b - 64))) << a);
  // } else {
  //   if (b < 64)
  //     erram ^= (((rram & (1LL << b)) >> b) << (a - 64));
  //   else
  //     erram ^= (((erram & (1LL << (b - 64))) >> (b - 64)) << (a - 64));
  // }
  auto x = peek(a);
  auto y = peek(b);
  poke(a, x ^ y);
}
inline void rcomp(int a, int _) { asm("btcq %1, %2" : "=r"(rram) : "IOr"((uint64_t)(a)), "r"(rram)); }
inline void ercomp(int a, int _) { asm("btcq %1, %2" : "=r"(erram) : "IOr"((uint64_t)(a)), "r"(erram)); }
inline void rclr(int a, int _) { asm("btrq %1, %2" : "=r"(rram) : "IOr"((uint64_t)(a)), "r"(rram)); }
inline void erclr(int a, int _) { asm("btrq %1, %2" : "=r"(erram) : "IOr"((uint64_t)(a)), "r"(erram)); }

inline void rcopy_ram(int a, int b) {
  auto ny = (rram & (0xFFFFLL << b)) >> b;
  rram &= ~(0xFFFFLL << a);
  rram |= ny << a;
}
inline void rcopy_pm(int a, int b) {
  rram &= ~(0xFFFFLL << a);
  rram |= pm[b] << a;
}
inline void rnop(int _, int __) {}

inline void syncPC() {
  pc = 0;
  for (int i = 0; i < 16; i++) pc |= peek(15 - i) << i;
}
inline void incPC() {
  volatile int res = 1;
  for (int i = 0; i < 16 && res; i++) asm("btcq %2, %3" : "=@ccc"(res), "=r"(rram) : "IOr"((uint_fast64_t)(15 - i)), "r"(rram));
}

char out_buffer = 0;
int out_buffer_count = 0;

using InstructionFunc = void (*)(int, int);
InstructionFunc instructions[] = {rcopy_ram, rcopy_pm, rnand, rxor, rclr, rcomp, rnop, rnop, rnop, rnop, rnop, erclr, ercomp};
void handleIO() {
  if (peek(19)) {
    out_buffer <<= 1;
    out_buffer &= ~1;
    out_buffer |= peek(18);
    out_buffer_count++;
    poke(19, 0);
  }
  if (out_buffer_count > 7) {
    std::cout << (out_buffer);
    out_buffer_count = 0;
  }
}
bool didModify = false;
std::vector<Op> program;
void ebs_delo() {
  while (true) {
    syncPC();
    incPC();
    auto qq = program[pc];
    instructions[qq.type](qq.a, qq.b);
    handleIO();
  }
}

int main(int argc, char **argv) {
  erram = 0;
  rram = 0;
  std::ifstream in("example3.out", std::ios::binary);
  uint16_t wer[128];
  in.read(reinterpret_cast<char *>(wer), 256);
  pm = wer;
  for (int n = 0; n < 128; n++) {
    uint16_t tmp = 0;
    for (int i = 0; i < 16; i++) {
      int bit_position = (i < 8) ? (7 - i) : (15 - (i - 8));
      auto ee = (bool)(wer[n] & (1 << bit_position));
      tmp |= ee << i;
    }
    wer[n] = tmp;
  }

  in.seekg(0, in.beg);
  uint16_t v;
  while (in.read(reinterpret_cast<char *>(&v), 2)) {
    v = (v >> 8) | (v << 8);
    auto cop = Op{.a = (v >> 2) & 0x7F, .b = ((v >> 9) & 0x7F), .type = (operacija)(v & 3), .ext = 0, .cp = false, .raw = v};
    if (cop.a == cop.b) cop.type = (cop.type == copy_ram ? nop : cop.type == xor_ ? clr : cop.type == nand ? comp : copy_pm);
    if (cop.type != nop && (cop.a < 16 || cop.b < 16)) cop.cp = true;
    if ((cop.type == clr || cop.type == comp) && cop.a > 63) {
      cop.a -= 64;
      cop.type = (operacija)(cop.type + 7);
    }
    program.push_back(cop);
  }
  ebs_delo();
}
