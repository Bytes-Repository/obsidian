#ifndef OBSIDIAN_SHIM_H
#define OBSIDIAN_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Layout MUSI być bit-w-bit identyczny z `HshArray` z
 * H-Sharp/source-code/compiler/runtime/core.c:
 *
 *     typedef struct {
 *         int64_t len;
 *         int64_t cap;
 *         int64_t data[1];   // flexible array member
 *     } HshArray;
 *
 * H# reprezentuje `[T]` (dowolny typ elementu, w tym `int`/uchwyty LLVM)
 * jako wskaźnik na tę strukturę, przekazywany przez granicę `extern` jako
 * gołe `int64_t` (bitcast wskaźnika — patrz llvm_types.rs::htype_to_llvm,
 * TypeExpr::Array => i64). Ten shim istnieje wyłącznie po to, by rozpakować
 * takie tablice do prawdziwych `LLVMValueRef*`/`LLVMTypeRef*`, których
 * oczekuje C-API LLVM-a (funkcje przyjmujące `Params, ParamCount` itp.),
 * bo `extern static/dynamic [c]` w H# nie potrafi samo zbudować takiej
 * tablicy — wszystkie elementy w H# i tak są jednym, jednolitym 8-bajtowym
 * slotem (patrz komentarz przy TypeExpr::Named w llvm_types.rs), więc
 * `int64_t*` = `LLVMValueRef*` = `LLVMTypeRef*` bit-w-bit.
 */
typedef struct {
    int64_t len;
    int64_t cap;
    int64_t data[1];
} HshArray;

static inline int64_t hsh_arr_len(const HshArray *a) {
    return a ? a->len : 0;
}

static inline const int64_t *hsh_arr_data(const HshArray *a) {
    static const int64_t empty = 0;
    return a ? a->data : &empty;
}

#ifdef __cplusplus
}
#endif

#endif /* OBSIDIAN_SHIM_H */
