#pragma once

// handy macros

#define tokconcat2(x,y) x##y
#define tokconcat(x,y) tokconcat2(x,y)
#define tokconcatU2(x,y) x##_##y
#define tokconcatU(x,y) tokconcatU2(x,y)

#define _get_nth_arg_50(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, N, ...) N
// extra indirection macro to workaround MSVC brokenness
#define __get_nth_arg_50(args) _get_nth_arg_50 args
#define count_macro_args(...) __get_nth_arg_50((__VA_ARGS__, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))

#define func_flags(...) _func_flags_msvc_workaround((__VA_ARGS__))
#define _func_flags_msvc_workaround(args) _func_flags args
#define _func_flags(...) _func_flags_dispatch_1(count_macro_args(__VA_ARGS__), (__VA_ARGS__))
#define _func_flags_dispatch_1(n, args) _func_flags_dispatch_2(n, args)
#define _func_flags_dispatch_2(n, args) _func_flags_dispatch_3(n, args)
#define _func_flags_dispatch_3(n, args) _func_flags_##n args
#define _func_flags_1(prefix) (0)
#define _func_flags_2(prefix, f1) (prefix##_##f1)
#define _func_flags_3(prefix, f1, f2) (prefix##_##f1 | prefix##_##f2)
#define _func_flags_4(prefix, f1, f2, f3) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3)
#define _func_flags_5(prefix, f1, f2, f3, f4) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4)
#define _func_flags_6(prefix, f1, f2, f3, f4, f5) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5)
#define _func_flags_7(prefix, f1, f2, f3, f4, f5, f6) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6)
#define _func_flags_8(prefix, f1, f2, f3, f4, f5, f6, f7) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7)
#define _func_flags_9(prefix, f1, f2, f3, f4, f5, f6, f7, f8) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7 | prefix##_##f8)
#define _func_flags_10(prefix, f1, f2, f3, f4, f5, f6, f7, f8, f9) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7 | prefix##_##f8 | prefix##_##f9)
#define _func_flags_11(prefix, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7 | prefix##_##f8 | prefix##_##f9 | prefix##_##f10)
#define _func_flags_12(prefix, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7 | prefix##_##f8 | prefix##_##f9 | prefix##_##f10 | prefix##_##f11)
#define _func_flags_13(prefix, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7 | prefix##_##f8 | prefix##_##f9 | prefix##_##f10 | prefix##_##f11 | prefix##_##f12)
#define _func_flags_14(prefix, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7 | prefix##_##f8 | prefix##_##f9 | prefix##_##f10 | prefix##_##f11 | prefix##_##f12 | prefix##_##f13)
#define _func_flags_15(prefix, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7 | prefix##_##f8 | prefix##_##f9 | prefix##_##f10 | prefix##_##f11 | prefix##_##f12 | prefix##_##f13 | prefix##_##f14)
#define _func_flags_16(prefix, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15) (prefix##_##f1 | prefix##_##f2 | prefix##_##f3 | prefix##_##f4 | prefix##_##f5 | prefix##_##f6 | prefix##_##f7 | prefix##_##f8 | prefix##_##f9 | prefix##_##f10 | prefix##_##f11 | prefix##_##f12 | prefix##_##f13 | prefix##_##f14 | prefix##_##f15)
