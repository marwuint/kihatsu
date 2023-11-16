#include <emmintrin.h>
#include <tmmintrin.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <vector>
std::array<bool, 128> ram{};
std::array<bool, 128 * 16> rom{};

enum op_type { cp_ram = 0, cp_rom = 1, nand = 2, xor_ = 3, clr = 4, comp = 5, nop = 6 };
struct Op {
  int a;
  int b;
  op_type type;
};

inline void mnand(int a, int b) { ram[a] = !(ram[a] & ram[b]); }
inline void mxor(int a, int b) { ram[a] ^= ram[b]; }
inline void mclr(int a, int b) { ram[a] = 0; }
inline void mcomp(int a, int b) { ram[a] = !ram[a]; }
inline void mcopy_ram(int a, int b) { _mm_storeu_si128(reinterpret_cast<__m128i *>(ram.data() + a), _mm_loadu_si128(reinterpret_cast<__m128i *>(ram.data() + b))); }
inline void mcopy_rom(int a, int b) { _mm_storeu_si128(reinterpret_cast<__m128i *>(ram.data() + a), _mm_loadu_si128(reinterpret_cast<__m128i *>(rom.data() + b * 16))); }
uint_fast16_t pc{};
const char shfArray[16] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
const __m128i shfI = _mm_lddqu_si128(reinterpret_cast<const __m128i *>(&shfArray));
// __m128i shfI = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
inline void syncPC() {
  auto qq = _mm_loadu_si128(reinterpret_cast<__m128i *>(ram.data()));
  qq = _mm_shuffle_epi8(qq, shfI);
  qq = _mm_slli_epi16(qq, 7);
  pc = _mm_movemask_epi8(qq);
}

const char rShf[16] = {15, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 0};
const __m128i rShfI = _mm_load_si128(reinterpret_cast<const __m128i *>(&rShf));
// const __m128i rShfI = _mm_set_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);

unsigned char mAndr[16] = {1, 1, 2, 2, 4, 4, 8, 8, 16, 16, 32, 32, 64, 64, 128, 128};
// const __m128i mAndRI = _mm_set_epi8(128, 128, 64, 64, 32, 32, 16, 16, 8, 8, 4, 4, 2, 2, 1, 1);
const __m128i mAndRI = _mm_load_si128(reinterpret_cast<const __m128i *>(&mAndr));
inline void putPc() {
  auto qq = _mm_set1_epi16(pc);
  qq = _mm_and_si128(qq, mAndRI);
  qq = _mm_cmpeq_epi8(qq, mAndRI);
  qq = _mm_shuffle_epi8(qq, rShfI);
  _mm_store_si128(reinterpret_cast<__m128i *>(ram.data()), qq);
}

inline void handleIO() {
  static char out_buffer = 0;
  static int out_buffer_c = 7;
  out_buffer <<= 1;
  out_buffer &= ~1;
  out_buffer |= ram[18];
  ram[19] = 0;
  if (!out_buffer_c--) {
    std::cout << (out_buffer);
    out_buffer_c = 7;
  }
}

void (*IT[])(int, int) = {mcopy_ram, mcopy_rom, mnand, mxor, mclr, mcomp};
std::vector<Op> program;
void looop() {
  bool modifiedPC{};

  while (true) {
    if (__builtin_expect(modifiedPC, 0)) {
      syncPC();
      modifiedPC = false;
    }
    auto qq = program[pc++];
    if (qq.type == nop) continue;
    modifiedPC = (qq.a < 16 || qq.b < 16);
    if (__builtin_expect(modifiedPC, 0)) putPc();
    IT[qq.type](qq.a, qq.b);
    if (qq.a <= 19 && qq.a > 3 && ram[19] & 1) handleIO();
  }
}

int main(int argc, char **argv) {
  std::ifstream in(argc > 1 ? argv[1] : "example3.out", std::ios::binary);
  auto fs = std::filesystem::file_size(argc > 1 ? argv[1] : "example3.out");
  auto tb = std::vector<uint16_t>(fs / 2);
  in.read(reinterpret_cast<char *>(tb.data()), fs);

#pragma clang loop unroll(disable)
  for (int n = 0; n < 128; n++)
#pragma clang loop vectorize(disable) unroll(disable)
    for (int i = 0; i < 16; i++) rom[n * 16 + i] = (tb[n] & (1 << ((i < 8) ? (7 - i) : (15 - (i - 8)))));

#pragma clang loop interleave(disable) vectorize(disable) unroll(disable)
  for (auto i : tb) {
    i = (i >> 8) | (i << 8);
    auto cop = Op{.a = ((i >> 2) & 0x7F), .b = ((i >> 9) & 0x7F), .type = (op_type)(i & 3)};
    if (cop.a == cop.b) cop.type = (cop.type == cp_ram ? nop : cop.type == xor_ ? clr : cop.type == nand ? comp : cp_rom);
    program.push_back(cop);
  }
  looop();
}
