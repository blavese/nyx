/* 32-bit x86 has no 64-bit divide instruction, so the compiler emits calls to
   these helpers. libgcc normally supplies them; a freestanding kernel has to. */
#include "types.h"

static u64 udivmod64(u64 n, u64 d, u64 *rem) {
    if (d == 0) { if (rem) *rem = 0; return 0; }   /* caller's problem */
    u64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (u64)1 << i; }
    }
    if (rem) *rem = r;
    return q;
}

u64 __udivdi3(u64 a, u64 b) { return udivmod64(a, b, 0); }
u64 __umoddi3(u64 a, u64 b) { u64 r; udivmod64(a, b, &r); return r; }

i64 __divdi3(i64 a, i64 b) {
    int neg = 0;
    u64 ua = (u64)a, ub = (u64)b;
    if (a < 0) { ua = (u64)(-a); neg ^= 1; }
    if (b < 0) { ub = (u64)(-b); neg ^= 1; }
    u64 q = udivmod64(ua, ub, 0);
    return neg ? -(i64)q : (i64)q;
}

i64 __moddi3(i64 a, i64 b) {
    int neg = a < 0;
    u64 ua = (u64)(a < 0 ? -a : a), ub = (u64)(b < 0 ? -b : b);
    u64 r; udivmod64(ua, ub, &r);
    return neg ? -(i64)r : (i64)r;
}

u64 __udivmoddi4(u64 a, u64 b, u64 *rem) { return udivmod64(a, b, rem); }
