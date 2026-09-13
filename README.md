# Obsidian

**Obsidian** to wrapper LLVM dla [H#](https://github.com/HackerOS-Linux-System/H-Sharp) —
odpowiednik tego, czym `inkwell` jest dla Rusta: cienka, idiomatyczna
warstwa nad C-API LLVM-a (`llvm-c`), zamiast wołania surowych funkcji
`LLVMBuildAdd`/`LLVMContextCreate` ręcznie.

```hsharp
mod context
mod module
mod types
mod builder

fn main() is
    let ctx: Context = Context::create()
    let m: Module     = ctx.create_module("hello")

    let i32_ty: Type = ctx.i32_type()
    let fn_ty:  Type = i32_ty.fn_type([i32_ty, i32_ty], false)
    let sum_fn: FunctionValue = m.add_function("sum", fn_ty)

    let entry: BasicBlock = sum_fn.append_basic_block(ctx, "entry")
    let b: Builder = ctx.create_builder()
    b.position_at_end(entry)
    b.build_ret(b.build_add(sum_fn.get_param(0), sum_fn.get_param(1), "result"))
    b.dispose()

    write(m.print_to_string())
    ctx.dispose()
end
```

## Wersjonowanie: jedno API, wiele LLVM-ów

Ta wersja (**0.1.x**) celuje w **LLVM 21** (ta sama wersja, pod którą
zbudowany jest sam kompilator H#). Plan na przyszłość:

| Obsidian | LLVM |
|----------|------|
| 0.1.x    | 21   |
| 0.2.x    | 22   |
| 0.3.x    | 23…  |

Każda kolejna wersja ma dostać **identyczne API wysokopoziomowe**
(`Context`/`Module`/`Builder`/`Type`/`Value`/...) — zmienia się tylko to,
co dzieje się pod spodem (surowe deklaracje w `src/ffi/*.h#` +
`native/obsidian_shim.c`, dopasowane do nowego C-API LLVM-a). Kod
korzystający z Obsidiana nie powinien wymagać zmian przy skoku na kolejną
wersję większą, jeśli tylko trzymasz się API z `src/` (nie własnych wołań
`LLVM*` bezpośrednio).

Jeśli LLVM w danej wersji usunie/zmieni jakąś funkcję C-API, dostanie to
osobny wpis w CHANGELOGu tej wersji Obsidiana — to jedyny wyjątek od
"identyczne API".

## Wymagania

- **H#** zbudowany z backendem LLVM 21 (`h# compile`, nie `h# preview` —
  `extern` działa tylko w ścieżce kompilacji AOT, nie w interpreterze;
  patrz "Dlaczego `h# preview` nie zadziała" niżej).
- **LLVM 21**, nagłówki + `llvm-config` (np. Debian/Ubuntu: `llvm-21-dev`;
  macOS + Homebrew: `llvm@21`).
- Zwykły `cc`/`ar` do zbudowania małego shimu C (patrz niżej) — żadnego
  dodatkowego build-systemu.

## Budowanie

```bash
# 1) Shim C — garstka funkcji pomocniczych do przekazywania tablic
#    (LLVMValueRef*, LLVMTypeRef*...) przez granicę FFI, patrz
#    "Dlaczego w ogóle jest tu C" niżej.
cd native
make                      # -> native/libobsidian_shim.a
cd ..

# 2) Powiedz linkerowi, GDZIE szukać `libobsidian_shim.a` i
#    `libLLVM-21.so` — `h# compile` nie ma żadnej opcji `-L`/`-l` w CLI
#    (sprawdź `h# compile --help`), więc jedyna droga to standardowa
#    zmienna środowiskowa `LIBRARY_PATH`, którą honoruje `cc`/`ld` pod
#    spodem (patrz komunikat kompilatora przy błędzie linkowania:
#    "check -L search paths include them (LIBRARY_PATH env var)").
#    Podmień ścieżkę do LLVM-a jeśli Twoja dystrybucja trzyma go gdzie
#    indziej (macOS+Homebrew: /opt/homebrew/opt/llvm@21/lib).
export LIBRARY_PATH="$PWD/native:/usr/lib/llvm-21/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"

# 3) Sama biblioteka (jako .a, patrz Bytes.hk: [build] -> emit => lib)
bytes build
```

**Jak to działa naprawdę:** `libLLVM-21.so` i `libobsidian_shim.a` NIE są
linkowane przez żaden klucz w `Bytes.hk` — biorą się WYŁĄCZNIE z bloków
`extern dynamic [c, "LLVM-21"]` / `extern static [c, "obsidian_shim"]` w
kodzie źródłowym (`src/ffi/*.h#`). Kompilator sam dodaje `-lLLVM-21` /
`-lobsidian_shim` do wywołania linkera na podstawie tych bloków (patrz
`compiler/src/ffi_linker.rs`) — Twoja jedyna rola to sprawić, żeby `cc`
w ogóle znalazł te pliki `.so`/`.a` na dysku, stąd `LIBRARY_PATH` powyżej.
`Bytes.hk`'s `[deps]` (`LLVM-21 => dynamic`, `obsidian_shim => static`)
to czysta dokumentacja dla `bytes info`/człowieka — `bytes` traktuje takie
wpisy jako "biblioteka systemowa, nic nie pobieraj" i **nie** przekłada
ich na żadną flagę kompilatora.

Jeśli po `export LIBRARY_PATH=...` linker nadal krzyczy `cannot find
-lLLVM-21` / `cannot find -lobsidian_shim`, sprawdź dokładnie gdzie Twój
menedżer pakietów postawił te pliki (`find / -name 'libLLVM-21*' 2>/dev/null`)
i dopisz właściwy katalog do `LIBRARY_PATH`.

## Budowanie przykładów i testu dymnego

`examples/` i `tests/` NIE duplikują plików biblioteki obok siebie —
zamiast tego korzystają z tego, że resolver modułów H# (`mod nazwa`,
patrz `compiler/src/modules.rs`) szuka pliku najpierw w katalogu pliku,
który go zadeklarował, a **w razie niepowodzenia — w katalogu roboczym
procesu (`cwd`)**. Kompiluj więc z `src/` jako katalogiem roboczym:

```bash
export LIBRARY_PATH="$PWD/native:/usr/lib/llvm-21/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
cd src
h# compile ../examples/hello_sum.h#   -o ../build/hello_sum
h# compile ../examples/emit_object.h# -o ../build/emit_object
h# compile ../tests/smoke_test.h#     -o ../build/smoke_test
cd ..

./build/smoke_test     # powinno skończyć się "=== WSZYSTKO OK ==="
./build/hello_sum       # drukuje IR + wynik JIT-owanego sum(2, 40)
./build/emit_object     # zapisuje answer.o w bieżącym katalogu
```

### Dlaczego `bytes test` nie jest wspierane

`bytes` wykonuje `#[test]`/`assert_*` przez swój własny, lekki JIT
(Cranelift, w pamięci) do szybkiej pętli dev — nie ma w nim kroku
"zlinkuj z `libLLVM-21.so` i `libobsidian_shim.a`". Obsidian z natury
rzeczy wymaga prawdziwego linkera, więc `tests/smoke_test.h#` jest
zwykłym programem z `fn main()`, kompilowanym i uruchamianym jak
`examples/` — nie plikiem pod `bytes test`.

### Dlaczego `h# preview` nie zadziała

`extern static`/`extern dynamic` to konstrukcja czasu kompilacji AOT
(`compiler/src/ffi.rs` + `codegen.rs` emitują prawdziwe wywołania LLVM
`call` do zadeklarowanego symbolu C) — interpreter (`h# preview`) nie ma
odpowiadającej mu ścieżki wykonania dla `extern`. Obsidian, jako wrapper
na kompilator, i tak ma sens wyłącznie w świecie AOT.

## Mapa modułów

| Plik | Odpowiednik w `inkwell` | Zawartość |
|---|---|---|
| `src/context.h#` | `context::Context` | Fabryki typów, moduł, builder, stałe |
| `src/module.h#` | `module::Module` | Funkcje/globalne, druk, weryfikacja, bitcode |
| `src/types.h#` | `types::*` | Introspekcja typów, struktury, stałe typu |
| `src/values.h#` | `values::*` (ogólne) | Nazwa, typ, linkage, stałe skalarne |
| `src/function.h#` | `values::FunctionValue` | Parametry, bloki, calling convention |
| `src/basic_block.h#` | `basic_block::BasicBlock` | Nawigacja po blokach, terminator |
| `src/builder.h#` | `builder::Builder` | ~70 metod `build_*` — cały zestaw instrukcji |
| `src/target_machine.h#` | `targets::*` | Target/TargetMachine/TargetData, emisja `.o`/`.s` |
| `src/execution_engine.h#` | `execution_engine::*` | MCJIT/interpreter, `GenericValue` |
| `src/pass_manager.h#` | `passes::*` | Nowy Pass Manager (`run_passes`/`optimize`) |
| `src/predicates.h#` | enumy z `IntPredicate`/`Linkage`/... | Stałe `i32`/`u32` |
| `src/conv.h#` | — (wewnętrzne) | `[Type]`/`[Value]`/`[BasicBlock]` ↔ `[int]` |
| `src/ffi/*.h#` | — (wewnętrzne) | Surowe `extern dynamic [c, "LLVM-21"]` |
| `native/obsidian_shim.c` | — (wewnętrzne) | Marshalling tablic dla garstki funkcji LLVM-C |

## Architektura / decyzje projektowe

Kilka rzeczy tu jest nietypowych względem "zwykłej" biblioteki H# — bo
LLVM-C to bardzo C-owe API (opaque wskaźniki, tablice `T*` + `unsigned
Count`, out-parametry `char**`), a H# jest świadomie prostszym, bezpieczniejszym
językiem bez adresowania `&mut`. Zamiast udawać, że tego napięcia nie ma,
Obsidian rozwiązuje je jawnie:

- **Uchwyty LLVM (`LLVMContextRef` i podobne) = `int` (i64) po stronie
  H#.** To bitcast wskaźnika, dokładnie ta sama konwencja, jakiej H# już
  używa wewnętrznie dla wskaźników opaque (patrz `llvm_types.rs`) —
  bezpieczne, bo i64 i wskaźnik mają identyczną reprezentację ABI na
  x86-64/aarch64.
- **`native/obsidian_shim.c`** istnieje WYŁĄCZNIE dla garstki funkcji
  LLVM-C przyjmujących tablicę (`LLVMBuildCall2`, `LLVMFunctionType`,
  `LLVMStructTypeInContext`, GEP, phi, stałe agregaty) albo zwracających
  wynik przez out-parametr (`LLVMGetTargetFromTriple`,
  `LLVMCreateExecutionEngineForModule`, `LLVMTargetMachineEmitToFile`) —
  H#'s `extern` nie ma jak zbudować `T*` z `[T]` ani `&mut` do lokalnej
  zmiennej. Shim zna dokładny layout `HshArray` z `compiler/runtime/
  core.c` (`{ len, cap, data[] }`) i rozpakowuje go do prawdziwych
  tablic C. **Cała reszta API** (>90% wywołań) idzie bezpośrednio z H# do
  `libLLVM-21.so`, bez przechodzenia przez ten plik.
- **Stan błędu "ostatniej operacji"** (`obsidian_last_error()`) dla
  funkcji, które w C zwracają błąd przez `char **` — jedyny sposób
  ujednolicenia takiego API bez adresowania po stronie H#. Nie jest
  wątkowo bezpieczny (globalny stan) — świadomy kompromis dla narzędzia
  kompilatorowego, zwykle jednowątkowego.
- **Zarządzanie pamięcią stringów zwracanych przez LLVM.** H#'s `extern`
  przekazuje zwrócony `char*` WPROST jako wartość `string`, bez
  kopiowania (patrz `codegen.rs` — zwykłe wywołanie `call`, żadnego
  automatycznego marshallingu). Funkcje takie jak `Module::print_to_string`,
  `Value::print`, `Type::print` zwracają bufor, którego LLVM oczekuje że
  zwolnisz (`LLVMDisposeMessage`) — Obsidian **celowo tego nie robi**
  (ryzyko use-after-free przy przedwczesnym zwolnieniu przewyższa koszt
  przecieku w krótko żyjącym procesie kompilatora). Jeśli budujesz
  długo działający proces (np. serwer kompilujący wiele modułów w pętli),
  miej to na uwadze.
- **`ExecutionEngine::run_function` używa reflektywnego
  `LLVMRunFunction`** (interpreter LLVM-a, argumenty jako `GenericValue`)
  zamiast prawdziwego wskaźnika funkcyjnego o konkretnym typie — bo H#
  nie ma odpowiednika Rustowego `unsafe extern "C" fn` + `transmute` dla
  dynamicznie budowanych sygnatur. Wolniejsze niż prawdziwy JIT-call, ale
  działa dla DOWOLNEJ sygnatury bez dodatkowego kodu per-przypadek. Dla
  surowej szybkości: `ExecutionEngine::function_address()` + własny
  mostek C o Twojej konkretnej sygnaturze (analogicznie do
  `obsidian_shim.c`).

## Zakres pokrycia (0.1.0)

Zaimplementowane (solidne jądro, ~250 funkcji wysokiego poziomu):
Context/Module/Type/Value/FunctionValue/BasicBlock, pełen zestaw
instrukcji arytmetycznych/logicznych/porównań/rzutowań/pamięci/kontroli
przepływu w `Builder`, stałe (int/float/string/tablica/struktura/wektor),
struktury nazwane i anonimowe, weryfikacja modułu/funkcji, druk IR,
bitcode, IRReader (round-trip), Target/TargetMachine/TargetData + emisja
`.o`/`.s`, ExecutionEngine (MCJIT/interpreter) + `GenericValue`, nowy
Pass Manager (`run_passes`/`optimize`).

Świadomie POZA zakresem 0.1.0 (PR-y mile widziane): debug info (`DIBuilder`),
metadata/atrybuty na instrukcjach, `invoke`/obsługa wyjątków poza samym
budowaniem instrukcji (landing pady itp.), ORC JIT (tylko legacy
MCJIT/interpreter), inline assembly, atomiki/`fence`, wektory skalowalne
(`vscale`), COMDAT/sekcje.

## Licencja

MIT — patrz `LICENSE`.
