#include "obsidian_shim.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/Error.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include <stdlib.h>
#include <string.h>

/* ── Stan błędu ostatniej operacji "kombinowanej" (handle + out-param) ─────
 * H# nie ma operatora adresu (`&lokalna_zmienna`) do budowania `T**`/`T*`
 * wskazujących na stos, więc każda funkcja LLVM-C, która zwraca wynik
 * PRZEZ out-param zamiast wprost (LLVMBool + `char **ErrorMessage`,
 * LLVMBool + `LLVMExecutionEngineRef *OutEE`, ...) musi przejść przez tu-
 * -stronę: shim woła prawdziwe LLVM-C API na stosie C, a stronie H#
 * zwraca prosty `int`/`string` + globalny "ostatni błąd" do odpytania
 * przez `obsidian_last_error()`. Niewątkowe (globalny stan) — świadomy
 * kompromis prostoty dla narzędzia kompilatorowego, zwykle jednowątkowego;
 * jeśli budujesz moduły LLVM równolegle na wielu wątkach, serializuj
 * dostęp do tych funkcji sam. */
static char *g_last_error = NULL;

static void set_last_error(char *owned_or_null) {
    if (g_last_error) free(g_last_error);
    g_last_error = owned_or_null;
}

char *obsidian_last_error(void) {
    return g_last_error ? strdup(g_last_error) : strdup("");
}

void obsidian_clear_error(void) {
    set_last_error(NULL);
}

/* ── Typy: funkcje i agregaty, które biorą tablicę LLVMTypeRef ─────────── */

LLVMTypeRef obsidian_function_type(LLVMTypeRef ret_ty, const HshArray *param_tys,
                                    int is_var_arg) {
    unsigned n = (unsigned)hsh_arr_len(param_tys);
    const int64_t *raw = hsh_arr_data(param_tys);
    return LLVMFunctionType(ret_ty, (LLVMTypeRef *)(void *)raw, n,
                             is_var_arg ? 1 : 0);
}

LLVMTypeRef obsidian_struct_type_in_context(LLVMContextRef ctx,
                                             const HshArray *elem_tys,
                                             int packed) {
    unsigned n = (unsigned)hsh_arr_len(elem_tys);
    const int64_t *raw = hsh_arr_data(elem_tys);
    return LLVMStructTypeInContext(ctx, (LLVMTypeRef *)(void *)raw, n,
                                    packed ? 1 : 0);
}

void obsidian_struct_set_body(LLVMTypeRef struct_ty, const HshArray *elem_tys,
                               int packed) {
    unsigned n = (unsigned)hsh_arr_len(elem_tys);
    const int64_t *raw = hsh_arr_data(elem_tys);
    LLVMStructSetBody(struct_ty, (LLVMTypeRef *)(void *)raw, n,
                       packed ? 1 : 0);
}

/* Zwraca tablicę typów parametrów danego typu funkcji jako nową HshArray
 * (heap, malloc — zwolnij tak jak każdą H#-ową tablicę: nic nie trzeba robić,
 * H# GC/arena zarządza tym tak samo jak wynikiem hsh_array_push). */
HshArray *obsidian_get_param_types(LLVMTypeRef fn_ty) {
    unsigned n = LLVMCountParamTypes(fn_ty);
    HshArray *out = (HshArray *)malloc(sizeof(int64_t) * 2 + sizeof(int64_t) * (n > 0 ? n : 1));
    out->len = n;
    out->cap = n > 0 ? n : 1;
    if (n > 0) {
        LLVMTypeRef *tmp = (LLVMTypeRef *)malloc(sizeof(LLVMTypeRef) * n);
        LLVMGetParamTypes(fn_ty, tmp);
        for (unsigned i = 0; i < n; i++) out->data[i] = (int64_t)(intptr_t)tmp[i];
        free(tmp);
    }
    return out;
}

/* ── Wartości: wywołania, GEP, phi, stałe agregaty ──────────────────────── */

LLVMValueRef obsidian_build_call(LLVMBuilderRef b, LLVMTypeRef fn_ty,
                                  LLVMValueRef fn, const HshArray *args,
                                  const char *name) {
    unsigned n = (unsigned)hsh_arr_len(args);
    const int64_t *raw = hsh_arr_data(args);
    return LLVMBuildCall2(b, fn_ty, fn, (LLVMValueRef *)(void *)raw, n, name);
}

LLVMValueRef obsidian_build_invoke(LLVMBuilderRef b, LLVMTypeRef fn_ty,
                                    LLVMValueRef fn, const HshArray *args,
                                    LLVMBasicBlockRef then_bb,
                                    LLVMBasicBlockRef catch_bb,
                                    const char *name) {
    unsigned n = (unsigned)hsh_arr_len(args);
    const int64_t *raw = hsh_arr_data(args);
    return LLVMBuildInvoke2(b, fn_ty, fn, (LLVMValueRef *)(void *)raw, n,
                             then_bb, catch_bb, name);
}

LLVMValueRef obsidian_build_gep(LLVMBuilderRef b, LLVMTypeRef elem_ty,
                                 LLVMValueRef ptr, const HshArray *indices,
                                 const char *name, int in_bounds) {
    unsigned n = (unsigned)hsh_arr_len(indices);
    const int64_t *raw = hsh_arr_data(indices);
    if (in_bounds) {
        return LLVMBuildInBoundsGEP2(b, elem_ty, ptr,
                                      (LLVMValueRef *)(void *)raw, n, name);
    }
    return LLVMBuildGEP2(b, elem_ty, ptr, (LLVMValueRef *)(void *)raw, n, name);
}

void obsidian_phi_add_incoming(LLVMValueRef phi, const HshArray *values,
                                const HshArray *blocks) {
    unsigned n = (unsigned)hsh_arr_len(values);
    if (n == 0 || n != (unsigned)hsh_arr_len(blocks)) return;
    const int64_t *vraw = hsh_arr_data(values);
    const int64_t *braw = hsh_arr_data(blocks);
    LLVMAddIncoming(phi, (LLVMValueRef *)(void *)vraw,
                     (LLVMBasicBlockRef *)(void *)braw, n);
}

LLVMValueRef obsidian_const_array(LLVMTypeRef elem_ty, const HshArray *vals) {
    unsigned n = (unsigned)hsh_arr_len(vals);
    const int64_t *raw = hsh_arr_data(vals);
    return LLVMConstArray2(elem_ty, (LLVMValueRef *)(void *)raw, (uint64_t)n);
}

LLVMValueRef obsidian_const_struct_in_context(LLVMContextRef ctx,
                                               const HshArray *vals,
                                               int packed) {
    unsigned n = (unsigned)hsh_arr_len(vals);
    const int64_t *raw = hsh_arr_data(vals);
    return LLVMConstStructInContext(ctx, (LLVMValueRef *)(void *)raw, n,
                                     packed ? 1 : 0);
}

LLVMValueRef obsidian_const_named_struct(LLVMTypeRef struct_ty,
                                          const HshArray *vals) {
    unsigned n = (unsigned)hsh_arr_len(vals);
    const int64_t *raw = hsh_arr_data(vals);
    return LLVMConstNamedStruct(struct_ty, (LLVMValueRef *)(void *)raw, n);
}

LLVMValueRef obsidian_const_vector(const HshArray *vals) {
    unsigned n = (unsigned)hsh_arr_len(vals);
    const int64_t *raw = hsh_arr_data(vals);
    return LLVMConstVector((LLVMValueRef *)(void *)raw, n);
}

/* `LLVMGetStructName` zwraca C-owy NULL (nie pusty string!) dla struktur
 * literałowych/anonimowych (`context.struct_type([...], false)` bez
 * uprzedniego `named_struct`) — jedyna funkcja w tym pliku, gdzie realnie
 * zdarza się to w normalnym użyciu, nie tylko przy błędzie. Patrz uwaga o
 * NULL w obsidian_verify_module. */
char *obsidian_struct_name_safe(LLVMTypeRef struct_ty) {
    const char *n = LLVMGetStructName(struct_ty);
    return strdup(n ? n : "");
}

/* Zwraca argumenty funkcji (LLVMValueRef dla każdego parametru) jako nową
 * HshArray, w kolejności deklaracji. */
HshArray *obsidian_get_params(LLVMValueRef fn) {
    unsigned n = LLVMCountParams(fn);
    HshArray *out = (HshArray *)malloc(sizeof(int64_t) * 2 + sizeof(int64_t) * (n > 0 ? n : 1));
    out->len = n;
    out->cap = n > 0 ? n : 1;
    if (n > 0) {
        LLVMValueRef *tmp = (LLVMValueRef *)malloc(sizeof(LLVMValueRef) * n);
        LLVMGetParams(fn, tmp);
        for (unsigned i = 0; i < n; i++) out->data[i] = (int64_t)(intptr_t)tmp[i];
        free(tmp);
    }
    return out;
}

/* ── Wygoda: verify + komunikat błędu w jednym wywołaniu ────────────────── */

/* Zwraca NULL jeśli moduł jest poprawny, w przeciwnym razie zwraca
 * zaalokowany przez malloc string z komunikatem diagnostycznym (H#-owy
 * `string` po stronie extern to zwykły `const char*` — patrz ffi.rs
 * type_to_c; H#'s runtime kopiuje go do własnej reprezentacji zaraz po
 * powrocie z extern, więc oryginalny bufor od LLVM-a możemy tu bezpiecznie
 * zwolnić od razu). */
/* UWAGA: nigdy nie zwracamy C-owego NULL jako H#-owy `string` — H#'s
 * `extern` przekazuje zwrócony `char*` WPROST jako wartość `string` (patrz
 * README.md, "Zarządzanie pamięcią"), bez żadnego nullchecku po stronie
 * runtime'u dla WARTOŚCI ZWRACANYCH (w przeciwieństwie do parametrów).
 * Sukces = pusty, prawdziwie zaalokowany string "", nigdy NULL. */
char *obsidian_verify_module(LLVMModuleRef m) {
    char *msg = NULL;
    LLVMBool bad = LLVMVerifyModule(m, LLVMReturnStatusAction, &msg);
    if (!bad) {
        if (msg) LLVMDisposeMessage(msg);
        return strdup("");
    }
    if (!msg) return strdup("(brak komunikatu od LLVMVerifyModule)");
    char *out = strdup(msg);
    LLVMDisposeMessage(msg);
    return out;
}

/* Jak obsidian_verify_module, ale dla pojedynczej funkcji. */
char *obsidian_verify_function(LLVMValueRef fn) {
    LLVMBool bad = LLVMVerifyFunction(fn, LLVMReturnStatusAction);
    if (!bad) return strdup("");
    return strdup("funkcja nie przeszła weryfikacji LLVM (LLVMVerifyFunction) — "
                  "uruchom Module::verify() po całym module, by zobaczyć pełny opis błędu");
}

/* ── Target / TargetMachine ──────────────────────────────────────────────
 * Wołane raz na proces przez Target::init_native() — spina
 * LLVMInitializeNativeTarget/AsmPrinter/AsmParser w jedno wywołanie.
 * Zwraca 0 = ok, != 0 = błąd. */
int obsidian_init_native_target(void) {
    int a = LLVMInitializeNativeTarget();
    int b = LLVMInitializeNativeAsmPrinter();
    int c = LLVMInitializeNativeAsmParser();
    return (a || b || c) ? 1 : 0;
}

/* Zwraca uchwyt LLVMTargetRef albo 0 przy błędzie (patrz obsidian_last_error). */
intptr_t obsidian_get_target_from_triple(const char *triple) {
    LLVMTargetRef target = NULL;
    char *err = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &err)) {
        set_last_error(err ? strdup(err) : strdup("LLVMGetTargetFromTriple: nieznany błąd"));
        if (err) LLVMDisposeMessage(err);
        return 0;
    }
    set_last_error(NULL);
    return (intptr_t)target;
}

/* file_type: 0 = LLVMAssemblyFile, 1 = LLVMObjectFile (patrz target_machine.h#).
 * Zwraca 0 = ok, 1 = błąd (komunikat w obsidian_last_error). */
int obsidian_target_machine_emit_to_file(LLVMTargetMachineRef tm, LLVMModuleRef m,
                                          const char *filename, int file_type) {
    char *err = NULL;
    LLVMCodeGenFileType ft = file_type ? LLVMObjectFile : LLVMAssemblyFile;
    /* LLVMTargetMachineEmitToFile chce char* (nie const char*) dla ścieżki. */
    char *mutable_path = strdup(filename);
    LLVMBool failed = LLVMTargetMachineEmitToFile(tm, m, mutable_path, ft, &err);
    free(mutable_path);
    if (failed) {
        set_last_error(err ? strdup(err) : strdup("LLVMTargetMachineEmitToFile: nieznany błąd"));
        if (err) LLVMDisposeMessage(err);
        return 1;
    }
    set_last_error(NULL);
    return 0;
}

/* Zwraca 0 = ok, 1 = błąd. */
int obsidian_print_module_to_file(LLVMModuleRef m, const char *filename) {
    char *err = NULL;
    if (LLVMPrintModuleToFile(m, filename, &err)) {
        set_last_error(err ? strdup(err) : strdup("LLVMPrintModuleToFile: nieznany błąd"));
        if (err) LLVMDisposeMessage(err);
        return 1;
    }
    set_last_error(NULL);
    return 0;
}

/* ── ExecutionEngine (MCJIT) ─────────────────────────────────────────────
 * Zwraca uchwyt LLVMExecutionEngineRef albo 0 przy błędzie. Przejmuje
 * własność `m` tak jak oryginalne LLVM-C API (nie wołaj LLVMDisposeModule
 * po sukcesie — silnik zwolni moduł sam przy DisposeExecutionEngine). */
intptr_t obsidian_create_execution_engine_for_module(LLVMModuleRef m) {
    LLVMExecutionEngineRef ee = NULL;
    char *err = NULL;
    if (LLVMCreateExecutionEngineForModule(&ee, m, &err)) {
        set_last_error(err ? strdup(err) : strdup("LLVMCreateExecutionEngineForModule: nieznany błąd"));
        if (err) LLVMDisposeMessage(err);
        return 0;
    }
    set_last_error(NULL);
    return (intptr_t)ee;
}

intptr_t obsidian_create_jit_compiler_for_module(LLVMModuleRef m, int opt_level) {
    LLVMExecutionEngineRef ee = NULL;
    char *err = NULL;
    if (LLVMCreateJITCompilerForModule(&ee, m, (unsigned)opt_level, &err)) {
        set_last_error(err ? strdup(err) : strdup("LLVMCreateJITCompilerForModule: nieznany błąd"));
        if (err) LLVMDisposeMessage(err);
        return 0;
    }
    set_last_error(NULL);
    return (intptr_t)ee;
}

intptr_t obsidian_create_interpreter_for_module(LLVMModuleRef m) {
    LLVMExecutionEngineRef ee = NULL;
    char *err = NULL;
    if (LLVMCreateInterpreterForModule(&ee, m, &err)) {
        set_last_error(err ? strdup(err) : strdup("LLVMCreateInterpreterForModule: nieznany błąd"));
        if (err) LLVMDisposeMessage(err);
        return 0;
    }
    set_last_error(NULL);
    return (intptr_t)ee;
}

/* Woła funkcję `fn` przez interpreter LLVM-a z listą LLVMGenericValueRef
 * (patrz src/execution_engine.h#'s GenericValue) i zwraca uchwyt wyniku
 * jako LLVMGenericValueRef (0 jeśli fn zwraca void — nie wołaj wtedy
 * generic_value_to_*). */
intptr_t obsidian_run_function(LLVMExecutionEngineRef ee, LLVMValueRef fn,
                                const HshArray *args) {
    unsigned n = (unsigned)hsh_arr_len(args);
    const int64_t *raw = hsh_arr_data(args);
    LLVMGenericValueRef result =
        LLVMRunFunction(ee, fn, n, (LLVMGenericValueRef *)(void *)raw);
    return (intptr_t)result;
}

/* ── IRReader: parsowanie tekstowego .ll z powrotem do modułu (round-trip,
 * przydatne w testach). Zwraca uchwyt modułu albo 0 przy błędzie. */
intptr_t obsidian_parse_ir_in_context(LLVMContextRef ctx, const char *ir_text) {
    LLVMMemoryBufferRef buf = LLVMCreateMemoryBufferWithMemoryRangeCopy(
        ir_text, strlen(ir_text), "obsidian_parse_ir");
    LLVMModuleRef m = NULL;
    char *err = NULL;
    if (LLVMParseIRInContext(ctx, buf, &m, &err)) {
        set_last_error(err ? strdup(err) : strdup("LLVMParseIRInContext: nieznany błąd"));
        if (err) LLVMDisposeMessage(err);
        return 0;
    }
    set_last_error(NULL);
    return (intptr_t)m;
}
