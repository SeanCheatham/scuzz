package scalui.compiler

// Stage-1 LLVM IR emitter (kernel dialect). Conts threaded via emit state List.

def progDefs(p: List): List = nodeExpr(p, 1)
def progMain(p: List): List = nodeExpr(p, 2)

def defName(d: List): String = nodeStr(d, 0)
def defParams(d: List): List = nodeExpr(d, 1)
def defRet(d: List): String = nodeStr(d, 2)
def defBody(d: List): List = nodeExpr(d, 3)

def paramName(p: String): String =
  paramNameFrom(Str.slice(p, 6, Str.len(p)))

def paramNameFrom(rest: String): String =
  Str.slice(rest, 0, indexOfChar(rest, 58, 0))

def paramType(p: String): String =
  paramTypeFrom(Str.slice(p, 6, Str.len(p)))

def paramTypeFrom(rest: String): String =
  Str.slice(rest, indexOfChar(rest, 58, 0) + 1, Str.len(rest))

def llvmTypeOf(ty: String): String =
  if (streq(ty, "Int") == 1) "i64"
  else if (streq(ty, "Bool") == 1) "i64"
  else "ptr"

def kindOfType(ty: String): String =
  if (streq(ty, "Int") == 1) "int"
  else if (streq(ty, "Bool") == 1) "int"
  else if (startsWith(ty, "IO[") == 1) kindOfIoType(ty)
  else "ptr"

def kindOfIoType(ty: String): String =
  if (streq(ty, "IO[Int]") == 1) "ioi" else "io"

def findDef(defs: List, name: String): List =
  if (List.isEmpty(defs) == 1) List.empty
  else if (streq(defName(List.head(defs)), name) == 1) List.head(defs)
  else findDef(List.tail(defs), name)

def strContains(strs: List, s: String): Int =
  if (List.isEmpty(strs) == 1) 0
  else if (streq(List.head(strs), s) == 1) 1
  else strContains(List.tail(strs), s)

def strIndexAt(strs: List, s: String, i: Int): Int =
  if (i >= List.len(strs)) 0
  else if (streq(List.at(strs, i), s) == 1) i
  else strIndexAt(strs, s, i + 1)

def collectPush(strs: List, s: String): List =
  if (strContains(strs, s) == 1) strs else List.cons(s, strs)

def collectExpr(e: List, strs: List): List =
  collectExprTag(exprTag(e), e, strs)

def collectExprTag(tag: String, e: List, strs: List): List =
  if (streq(tag, "StrLit") == 1) collectPush(strs, nodeStr(e, 0))
  else if (streq(tag, "Println") == 1) collectExpr(nodeExpr(e, 0), strs)
  else if (streq(tag, "Fail") == 1) collectExpr(nodeExpr(e, 0), strs)
  else if (streq(tag, "Pure") == 1) collectExpr(nodeExpr(e, 0), strs)
  else if (streq(tag, "Sleep") == 1) collectExpr(nodeExpr(e, 0), strs)
  else if (streq(tag, "FlatMap") == 1)
    collectExpr(nodeExpr(e, 2), collectExpr(nodeExpr(e, 0), strs))
  else if (streq(tag, "Handle") == 1)
    collectExpr(nodeExpr(e, 1), collectExpr(nodeExpr(e, 0), strs))
  else if (streq(tag, "Attempt") == 1) collectExpr(nodeExpr(e, 0), strs)
  else if (streq(tag, "Let") == 1)
    collectExpr(nodeExpr(e, 2), collectExpr(nodeExpr(e, 1), strs))
  else if (streq(tag, "If") == 1)
    collectExpr(
      nodeExpr(e, 2),
      collectExpr(nodeExpr(e, 1), collectExpr(nodeExpr(e, 0), strs))
    )
  else if (streq(tag, "BinOp") == 1)
    collectExpr(nodeExpr(e, 2), collectExpr(nodeExpr(e, 1), strs))
  else if (streq(tag, "Call") == 1) collectArgs(nodeExpr(e, 1), strs)
  else if (streq(tag, "IoRace") == 1)
    collectExpr(nodeExpr(e, 1), collectExpr(nodeExpr(e, 0), strs))
  else if (streq(tag, "IoBoth") == 1)
    collectExpr(nodeExpr(e, 1), collectExpr(nodeExpr(e, 0), strs))
  else strs

def collectArgs(args: List, strs: List): List =
  if (List.isEmpty(args) == 1) strs
  else collectArgs(List.tail(args), collectExpr(List.head(args), strs))

def collectDefs(defs: List, strs: List): List =
  if (List.isEmpty(defs) == 1) strs
  else collectDefs(List.tail(defs), collectExpr(defBody(List.head(defs)), strs))

def collectProgram(prog: List): List =
  List.reverse(collectExpr(progMain(prog), collectDefs(progDefs(prog), List.empty)))

def emitStrConsts(strs: List, i: Int, acc: String): String =
  if (i >= List.len(strs)) acc
  else emitStrConsts(strs, i + 1, Str.concat(acc, emitOneStr(List.at(strs, i), i)))

def emitOneStr(s: String, i: Int): String =
  str4(
    str4("@.str", Str.fromInt(i), " = private unnamed_addr constant [", Str.fromInt(Str.len(s) + 1)),
    " x i8] c\"",
    llvmEscape(s),
    "\\00\", align 1\n"
  )

def runtimeDeclares(): String =
  Str.concat(runtimeDeclaresA(), Str.concat(runtimeDeclaresB(), runtimeDeclaresC()))

def runtimeDeclaresA(): String =
  str5(
    "declare ptr @su_string_from_cstr(ptr)\ndeclare ptr @su_string_cstr(ptr)\n",
    "declare ptr @su_string_concat(ptr, ptr)\ndeclare i64 @su_string_len(ptr)\n",
    "declare ptr @su_string_slice(ptr, i64, i64)\ndeclare i32 @su_string_eq(ptr, ptr)\n",
    "declare i64 @su_string_char_at(ptr, i64)\ndeclare ptr @su_string_from_int(i64)\n",
    str5(
      "declare i64 @su_string_index_of(ptr, ptr)\ndeclare ptr @su_io_println(ptr)\n",
      "declare ptr @su_io_pure(ptr)\ndeclare ptr @su_io_delay(ptr, ptr)\n",
      "declare ptr @su_io_flatmap(ptr, ptr, ptr)\ndeclare ptr @su_io_fail_cstr(ptr)\n",
      "declare ptr @su_io_sleep_ms(i64)\ndeclare ptr @su_io_handle_error_with(ptr, ptr, ptr)\n",
      str5(
        "declare ptr @su_io_attempt(ptr)\ndeclare ptr @su_io_race(ptr, ptr)\n",
        "declare ptr @su_io_both(ptr, ptr)\ndeclare ptr @su_adt_new(i32, ptr)\n",
        "declare i32 @su_adt_tag(ptr)\ndeclare ptr @su_lexer_classify(ptr)\n",
        "declare ptr @su_effects_run_kit()\ndeclare ptr @su_ui_run_headless_label(ptr, i32, i32)\n",
        "declare ptr @su_ui_run_counter(i32, i32)\ndeclare ptr @su_ui_run_live(i32, i32)\n"
      )
    )
  )

def runtimeDeclaresB(): String =
  str5(
    "declare ptr @su_ui_run_todo(i32, i32)\ndeclare ptr @su_list_nil()\n",
    "declare i32 @su_list_is_empty(ptr)\ndeclare ptr @su_list_cons(ptr, ptr)\n",
    "declare ptr @su_list_head(ptr)\ndeclare ptr @su_list_tail(ptr)\n",
    "declare i64 @su_list_len(ptr)\ndeclare ptr @su_list_at(ptr, i64)\n",
    str5(
      "declare ptr @su_list_reverse(ptr)\ndeclare ptr @su_list_join(ptr, ptr)\n",
      "declare ptr @su_fs_read(ptr)\ndeclare ptr @su_fs_write(ptr, ptr)\n",
      "declare ptr @su_fs_list(ptr)\ndeclare ptr @su_fs_mkdirs(ptr)\n",
      "declare ptr @su_sys_args()\ndeclare ptr @su_sys_exec(ptr)\n",
      str5(
        "declare ptr @su_sys_getenv(ptr)\ndeclare ptr @su_clock_real_time()\n",
        "declare ptr @su_clock_monotonic()\ndeclare ptr @su_random_next_int(i64)\n",
        "declare ptr @su_net_http_get(ptr)\ndeclare ptr @su_impurity_run_kit()\n",
        "declare ptr @su_lang_signal_int(i64)\ndeclare i64 @su_lang_signal_get(ptr)\n",
        "declare ptr @su_lang_signal_set(ptr, i64)\ndeclare ptr @su_lang_signal_str(ptr)\n"
      )
    )
  )

def runtimeDeclaresC(): String =
  str5(
    "declare ptr @su_lang_view_text(ptr)\ndeclare ptr @su_lang_view_text_signal(ptr, ptr)\n",
    "declare ptr @su_lang_view_button_inc(ptr, ptr)\ndeclare ptr @su_lang_view_button_set(ptr, ptr, i64)\n",
    "declare ptr @su_lang_view_column()\ndeclare ptr @su_lang_view_row()\n",
    "declare ptr @su_lang_view_list()\ndeclare ptr @su_lang_view_scroll(ptr)\n",
    str5(
      "declare ptr @su_lang_view_text_field(ptr, ptr)\ndeclare ptr @su_lang_view_icon(i64, i64)\n",
      "declare ptr @su_lang_view_image(i64, i64, i64, ptr)\ndeclare ptr @su_lang_view_add_child(ptr, ptr)\n",
      "declare ptr @su_lang_view_show_when(ptr, i64, ptr)\ndeclare ptr @su_lang_todo_create()\n",
      "declare ptr @su_lang_todo_load(ptr)\ndeclare ptr @su_lang_todo_draft(ptr)\n",
      str5(
        "declare ptr @su_lang_todo_list_view(ptr)\ndeclare ptr @su_lang_view_button_todo_add(ptr, ptr)\n",
        "declare ptr @su_lang_view_button_todo_save(ptr, ptr)\ndeclare ptr @su_ui_run_view(ptr)\n",
        "declare ptr @su_ui_run_view_todo(ptr, ptr)\ndeclare ptr @su_box_i64(i64)\n",
        "declare i64 @su_unbox_i64(ptr)\ndeclare i32 @su_runtime_main_args(ptr, i32, ptr)\n\n",
        ""
      )
    )
  )

def delayThunk(): String =
  "define internal ptr @su_delay_unit_thunk(ptr %env) {\nentry:\n  ret ptr null\n}\n\n"

def emitHeader(strs: List): String =
  str4(
    "; ScalUI Stage-1 generated LLVM IR\n",
    "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n",
    "target triple = \"x86_64-pc-linux-gnu\"\n\n",
    Str.concat(runtimeDeclares(), Str.concat(emitStrConsts(strs, 0, "\n"), Str.concat("\n", delayThunk())))
  )

def ensureIoPair(code: String, kind: String, value: String, tmp: String): List =
  if (isIoKind(kind) == 1) pair(code, value)
  else if (streq(kind, "int") == 1)
    pair(
      str4(
        code,
        "  %",
        tmp,
        Str.concat(str4("_box = call ptr @su_box_i64(i64 ", value, ")\n  %", tmp), str3(" = call ptr @su_io_pure(ptr %", tmp, "_box)\n"))
      ),
      Str.concat("%", tmp)
    )
  else
    pair(
      str4(code, "  %", tmp, str3(" = call ptr @su_io_pure(ptr ", value, ")\n")),
      Str.concat("%", tmp)
    )

def emitSuString(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  if (streq(exprTag(e), "StrLit") == 1) emitSuStringLit(strs, nodeStr(e, 0), prefix, id, conts)
  else emitSuStringExpr(strs, e, defs, env, prefix, id, conts)

def emitSuStringLit(strs: List, s: String, prefix: String, id: Int, conts: String): List =
  val idx = strIndexAt(strs, s, 0)
  val len = Str.len(s) + 1
  val code = str4(
    "  %",
    prefix,
    "_gep = getelementptr inbounds [",
    str4(
      Str.fromInt(len),
      " x i8], ptr @.str",
      Str.fromInt(idx),
      ", i64 0, i64 0\n  %"
    )
  )
  val code2 = str4(code, prefix, "_ss = call ptr @su_string_from_cstr(ptr %", str3(prefix, "_gep", ")\n"))
  mkS(code2, str3("%", prefix, "_ss"), "ptr", id, conts)

def emitSuStringExpr(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  emitExpr(e, strs, defs, env, Str.concat(prefix, "_e"), id, conts)

def emitCstr(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  if (streq(exprTag(e), "StrLit") == 1) emitCstrLit(strs, nodeStr(e, 0), prefix, id, conts)
  else emitCstrExpr(strs, e, defs, env, prefix, id, conts)

def emitCstrLit(strs: List, s: String, prefix: String, id: Int, conts: String): List =
  val idx = strIndexAt(strs, s, 0)
  val len = Str.len(s) + 1
  val code = str4(
    "  %",
    prefix,
    "_cstr = getelementptr inbounds [",
    str4(Str.fromInt(len), " x i8], ptr @.str", Str.fromInt(idx), ", i64 0, i64 0\n")
  )
  mkS(code, str3("%", prefix, "_cstr"), "ptr", id, conts)

def emitCstrExpr(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val se = emitExpr(e, strs, defs, env, Str.concat(prefix, "_s"), id, conts)
  mkS(
    str4(sCode(se), "  %", prefix, str3("_cstr = call ptr @su_string_cstr(ptr ", sValue(se), ")\n")),
    str3("%", prefix, "_cstr"),
    "ptr",
    sId(se),
    sConts(se)
  )

def emitExpr(e: List, strs: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  emitExprTag(exprTag(e), e, strs, defs, env, prefix, id, conts)

def emitExprTag(tag: String, e: List, strs: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  if (streq(tag, "Unit") == 1) mkS("", "null", "ptr", id, conts)
  else if (streq(tag, "IntLit") == 1) mkS("", nodeStr(e, 0), "int", id, conts)
  else if (streq(tag, "StrLit") == 1) emitSuStringLit(strs, nodeStr(e, 0), prefix, id, conts)
  else if (streq(tag, "Var") == 1)
    mkS("", envGetVal(env, nodeStr(e, 0)), envGetKind(env, nodeStr(e, 0)), id, conts)
  else if (streq(tag, "Delay") == 1)
    mkS(
      str3("  %", prefix, "_delay = call ptr @su_io_delay(ptr @su_delay_unit_thunk, ptr null)\n"),
      str3("%", prefix, "_delay"),
      "io",
      id,
      conts
    )
  else if (streq(tag, "Println") == 1) emitPrintln(strs, nodeExpr(e, 0), defs, env, prefix, id, conts)
  else if (streq(tag, "Fail") == 1) emitFail(strs, nodeExpr(e, 0), defs, env, prefix, id, conts)
  else if (streq(tag, "Pure") == 1) emitPure(strs, nodeExpr(e, 0), defs, env, prefix, id, conts)
  else if (streq(tag, "Sleep") == 1) emitSleep(strs, nodeExpr(e, 0), defs, env, prefix, id, conts)
  else if (streq(tag, "Let") == 1) emitLet(strs, e, defs, env, prefix, id, conts)
  else if (streq(tag, "If") == 1) emitIf(strs, e, defs, env, prefix, id, conts)
  else if (streq(tag, "BinOp") == 1) emitBinOp(strs, e, defs, env, prefix, id, conts)
  else if (streq(tag, "Call") == 1) emitCall(strs, e, defs, env, prefix, id, conts)
  else if (streq(tag, "FlatMap") == 1) emitFlatMap(strs, e, defs, env, prefix, id, conts)
  else if (streq(tag, "Handle") == 1) emitHandle(strs, e, defs, env, prefix, id, conts)
  else if (streq(tag, "Attempt") == 1) emitAttempt(strs, e, defs, env, prefix, id, conts)
  else if (streq(tag, "IoRace") == 1) emitRaceBoth(strs, e, defs, env, prefix, id, conts, "race")
  else if (streq(tag, "IoBoth") == 1) emitRaceBoth(strs, e, defs, env, prefix, id, conts, "both")
  else if (streq(tag, "Adt") == 1) emitAdt(e, prefix, id, conts)
  else mkS("", "null", "ptr", id, conts)

def emitPrintln(strs: List, arg: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val se = emitSuString(strs, arg, defs, env, prefix, id, conts)
  mkS(
    str4(sCode(se), "  %", prefix, str3("_io = call ptr @su_io_println(ptr ", sValue(se), ")\n")),
    str3("%", prefix, "_io"),
    "io",
    sId(se),
    sConts(se)
  )

def emitFail(strs: List, arg: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val ce = emitCstr(strs, arg, defs, env, prefix, id, conts)
  mkS(
    str4(sCode(ce), "  %", prefix, str3("_io = call ptr @su_io_fail_cstr(ptr ", sValue(ce), ")\n")),
    str3("%", prefix, "_io"),
    "io",
    sId(ce),
    sConts(ce)
  )

def emitPure(strs: List, inner: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val ie = emitExpr(inner, strs, defs, env, Str.concat(prefix, "_p"), id, conts)
  emitPureCont(ie, prefix)

def emitPureCont(ie: List, prefix: String): List =
  if (streq(sKind(ie), "int") == 1)
    mkS(
      str4(
        sCode(ie),
        "  %",
        prefix,
        str6("_box = call ptr @su_box_i64(i64 ", sValue(ie), ")\n  %", prefix, "_io = call ptr @su_io_pure(ptr %", str3(prefix, "_box", ")\n"))
      ),
      str3("%", prefix, "_io"),
      "ioi",
      sId(ie),
      sConts(ie)
    )
  else if (isIoKind(sKind(ie)) == 1)
    mkS(
      str4(sCode(ie), "  %", prefix, str3("_io = call ptr @su_io_pure(ptr ", sValue(ie), ")\n")),
      str3("%", prefix, "_io"),
      sKind(ie),
      sId(ie),
      sConts(ie)
    )
  else
    mkS(
      str4(sCode(ie), "  %", prefix, str3("_io = call ptr @su_io_pure(ptr ", sValue(ie), ")\n")),
      str3("%", prefix, "_io"),
      "io",
      sId(ie),
      sConts(ie)
    )

def emitSleep(strs: List, ms: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val me = emitExpr(ms, strs, defs, env, Str.concat(prefix, "_ms"), id, conts)
  emitSleepCont(me, prefix)

def emitSleepCont(me: List, prefix: String): List =
  if (streq(sKind(me), "int") == 1)
    mkS(
      str4(sCode(me), "  %", prefix, str3("_sleep = call ptr @su_io_sleep_ms(i64 ", sValue(me), ")\n")),
      str3("%", prefix, "_sleep"),
      "io",
      sId(me),
      sConts(me)
    )
  else
    mkS(
      str5(sCode(me), "  %", prefix, "_ms0 = add i64 0, 0\n  %", str4(prefix, "_sleep = call ptr @su_io_sleep_ms(i64 %", prefix, "_ms0)\n")),
      str3("%", prefix, "_sleep"),
      "io",
      sId(me),
      sConts(me)
    )

def emitLet(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val name = nodeStr(e, 0)
  val ve = emitExpr(nodeExpr(e, 1), strs, defs, env, str4(prefix, "_lv_", name, ""), id, conts)
  val env2 = envPut(env, name, sValue(ve), sKind(ve))
  val be = emitExpr(nodeExpr(e, 2), strs, defs, env2, str4(prefix, "_l_", name, ""), sId(ve), sConts(ve))
  mkS(Str.concat(sCode(ve), sCode(be)), sValue(be), sKind(be), sId(be), sConts(be))

def emitIf(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val ce = emitExpr(nodeExpr(e, 0), strs, defs, env, Str.concat(prefix, "_ic"), id, conts)
  emitIfAfterCond(strs, e, defs, env, prefix, ce)

def emitIfAfterCond(strs: List, e: List, defs: List, env: List, prefix: String, ce: List): List =
  val id = sId(ce)
  val cond = if (streq(sKind(ce), "int") == 1) sValue(ce) else str3("%", prefix, "_c0")
  val pre = if (streq(sKind(ce), "int") == 1) sCode(ce)
    else str4(sCode(ce), "  %", prefix, "_c0 = add i64 0, 0\n")
  val thenL = str4(prefix, "_then_", Str.fromInt(id), "")
  val elseL = str4(prefix, "_else_", Str.fromInt(id), "")
  val thenJ = str4(prefix, "_tj_", Str.fromInt(id), "")
  val elseJ = str4(prefix, "_ej_", Str.fromInt(id), "")
  val merge = str4(prefix, "_merge_", Str.fromInt(id), "")
  val head = str4(
    pre,
    "  %",
    prefix,
    str5("_cmp = icmp ne i64 ", cond, ", 0\n  br i1 %", prefix, str5("_cmp, label %", thenL, ", label %", elseL, "\n"))
  )
  val te = emitExpr(nodeExpr(e, 1), strs, defs, env, Str.concat(prefix, Str.concat("_t", Str.fromInt(id))), id + 1, sConts(ce))
  val ee = emitExpr(nodeExpr(e, 2), strs, defs, env, Str.concat(prefix, Str.concat("_e", Str.fromInt(id))), sId(te), sConts(te))
  val ty = if (streq(sKind(te), "int") == 1) "i64" else "ptr"
  val body = str4(
    head,
    thenL,
    ":\n",
    str4(
      sCode(te),
      "  br label %",
      thenJ,
      str5("\n", thenJ, ":\n  br label %", merge, str4(
          "\n",
          elseL,
          ":\n",
          str4(
            sCode(ee),
            "  br label %",
            elseJ,
            str5("\n", elseJ, ":\n  br label %", merge, str5("\n", merge, ":\n  %", prefix, str5("_phi = phi ", ty, " [ ", sValue(te), str5(", %", thenJ, " ], [ ", sValue(ee), str3(", %", elseJ, " ]\n")))))
          )
        ))
    )
  )
  mkS(body, str3("%", prefix, "_phi"), sKind(te), sId(ee), sConts(ee))

def emitBinOp(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val op = nodeStr(e, 0)
  val le = emitExpr(nodeExpr(e, 1), strs, defs, env, Str.concat(prefix, "_l"), id, conts)
  val re = emitExpr(nodeExpr(e, 2), strs, defs, env, Str.concat(prefix, "_r"), sId(le), sConts(le))
  emitBinOpCont(op, le, re, prefix)

def emitBinOpCont(op: String, le: List, re: List, prefix: String): List =
  if (streq(op, "+") == 1)
    if (streq(sKind(le), "ptr") == 1)
      if (streq(sKind(re), "ptr") == 1)
        mkS(
          str5(sCode(le), sCode(re), "  %", prefix, str5("_add = call ptr @su_string_concat(ptr ", sValue(le), ", ptr ", sValue(re), ")\n")),
          str3("%", prefix, "_add"),
          "ptr",
          sId(re),
          sConts(re)
        )
      else emitBinOpInt(op, le, re, prefix)
    else emitBinOpInt(op, le, re, prefix)
  else if (streq(op, "==") == 1)
    if (streq(sKind(le), "ptr") == 1)
      if (streq(sKind(re), "ptr") == 1) emitBinOpStrEq(le, re, prefix, 1)
      else emitBinOpInt(op, le, re, prefix)
    else emitBinOpInt(op, le, re, prefix)
  else if (streq(op, "!=") == 1)
    if (streq(sKind(le), "ptr") == 1)
      if (streq(sKind(re), "ptr") == 1) emitBinOpStrEq(le, re, prefix, 0)
      else emitBinOpInt(op, le, re, prefix)
    else emitBinOpInt(op, le, re, prefix)
  else emitBinOpInt(op, le, re, prefix)

def emitBinOpStrEq(le: List, re: List, prefix: String, isEq: Int): List =
  val code0 = str5(sCode(le), sCode(re), "  %", prefix, str5("_eqi = call i32 @su_string_eq(ptr ", sValue(le), ", ptr ", sValue(re), ")\n  %"))
  val code1 = str4(code0, prefix, "_eq = zext i32 %", str3(prefix, "_eqi", " to i64\n"))
  if (isEq == 1) mkS(code1, str3("%", prefix, "_eq"), "int", sId(re), sConts(re))
  else
    mkS(
      str4(
        code1,
        "  %",
        prefix,
        str6("_ne = icmp eq i64 %", prefix, "_eq, 0\n  %", prefix, "_nev = zext i1 %", str3(prefix, "_ne", " to i64\n"))
      ),
      str3("%", prefix, "_nev"),
      "int",
      sId(re),
      sConts(re)
    )

def emitBinOpInt(op: String, le: List, re: List, prefix: String): List =
  val lv = if (streq(sKind(le), "int") == 1) sValue(le) else str3("%", prefix, "_l0")
  val rv = if (streq(sKind(re), "int") == 1) sValue(re) else str3("%", prefix, "_r0")
  val c0 = Str.concat(sCode(le), sCode(re))
  val c1 = if (streq(sKind(le), "int") == 1) c0 else str4(c0, "  %", prefix, "_l0 = add i64 0, 0\n")
  val c2 = if (streq(sKind(re), "int") == 1) c1 else str4(c1, "  %", prefix, "_r0 = add i64 0, 0\n")
  emitBinOpIntOp(op, c2, lv, rv, prefix, sId(re), sConts(re))

def emitBinOpIntOp(op: String, code: String, lv: String, rv: String, prefix: String, id: Int, conts: String): List =
  if (streq(op, "+") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = add i64 ", lv, ", ", rv, "\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(op, "-") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = sub i64 ", lv, ", ", rv, "\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(op, "*") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = mul i64 ", lv, ", ", rv, "\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(op, "/") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = sdiv i64 ", lv, ", ", rv, "\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(op, "%") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = srem i64 ", lv, ", ", rv, "\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(op, "&&") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = and i64 ", lv, ", ", rv, "\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(op, "||") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = or i64 ", lv, ", ", rv, "\n")), str3("%", prefix, "_v"), "int", id, conts)
  else emitIcmp(op, code, lv, rv, prefix, id, conts)

def icmpPred(op: String): String =
  if (streq(op, "==") == 1) "eq"
  else if (streq(op, "!=") == 1) "ne"
  else if (streq(op, "<") == 1) "slt"
  else if (streq(op, "<=") == 1) "sle"
  else if (streq(op, ">") == 1) "sgt"
  else if (streq(op, ">=") == 1) "sge"
  else "eq"

def emitIcmp(op: String, code: String, lv: String, rv: String, prefix: String, id: Int, conts: String): List =
  mkS(
    str4(
      code,
      "  %",
      prefix,
      str5("_cmp = icmp ", icmpPred(op), " i64 ", lv, str6(", ", rv, "\n  %", prefix, "_v = zext i1 %", str3(prefix, "_cmp", " to i64\n")))
    ),
    str3("%", prefix, "_v"),
    "int",
    id,
    conts
  )

def emitAdt(e: List, prefix: String, id: Int, conts: String): List =
  mkS(
    str3("  %", prefix, "_adt = call ptr @su_adt_new(i32 0, ptr null)\n"),
    str3("%", prefix, "_adt"),
    "ptr",
    id,
    conts
  )

def emitAttempt(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val ie = emitExpr(nodeExpr(e, 0), strs, defs, env, Str.concat(prefix, "_at"), id, conts)
  val p = ensureIoPair(sCode(ie), sKind(ie), sValue(ie), Str.concat(prefix, "_atio"))
  mkS(
    str4(fst(p), "  %", prefix, str3("_attempt = call ptr @su_io_attempt(ptr ", snd(p), ")\n")),
    str3("%", prefix, "_attempt"),
    "io",
    sId(ie),
    sConts(ie)
  )

def emitRaceBoth(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String, which: String): List =
  val le = emitExpr(nodeExpr(e, 0), strs, defs, env, Str.concat(prefix, "_rl"), id, conts)
  val re = emitExpr(nodeExpr(e, 1), strs, defs, env, Str.concat(prefix, "_rr"), sId(le), sConts(le))
  val lp = ensureIoPair(sCode(le), sKind(le), sValue(le), Str.concat(prefix, "_rlio"))
  val rp = ensureIoPair(Str.concat(fst(lp), sCode(re)), sKind(re), sValue(re), Str.concat(prefix, "_rrio"))
  val fn = if (streq(which, "race") == 1) "@su_io_race" else "@su_io_both"
  mkS(
    str4(fst(rp), "  %", prefix, str5("_v = call ptr ", fn, "(ptr ", snd(lp), str3(", ptr ", snd(rp), ")\n"))),
    str3("%", prefix, "_v"),
    "io",
    sId(re),
    sConts(re)
  )

def nameInList(names: List, n: String): Int =
  if (List.isEmpty(names) == 1) 0
  else if (streq(List.head(names), n) == 1) 1
  else nameInList(List.tail(names), n)

def captureNames(env: List, acc: List): List =
  if (List.isEmpty(env) == 1) List.reverse(acc)
  else captureNamesCont(envBindName(List.head(env)), List.tail(env), acc)

def captureNamesCont(n: String, rest: List, acc: List): List =
  if (nameInList(acc, n) == 1) captureNames(rest, acc)
  else captureNames(rest, List.cons(n, acc))

def packEnvRev(revNames: List, env: List, prefix: String, i: Int, code: String, cur: String): List =
  if (List.isEmpty(revNames) == 1) pair(code, cur)
  else packEnvRevOne(List.head(revNames), List.tail(revNames), env, prefix, i, code, cur)

def packEnvRevOne(n: String, rest: List, env: List, prefix: String, i: Int, code: String, cur: String): List =
  val v = envGetVal(env, n)
  val k = envGetKind(env, n)
  if (streq(k, "int") == 1)
    packEnvRev(
      rest,
      env,
      prefix,
      i + 1,
      str4(
        code,
        "  %",
        prefix,
        str5(
          "_b",
          Str.fromInt(i),
          " = call ptr @su_box_i64(i64 ",
          v,
          str4(
            ")\n  %",
            prefix,
            "_",
            str5(Str.fromInt(i + 1), " = call ptr @su_list_cons(ptr %", prefix, "_b", str4(Str.fromInt(i), ", ptr ", cur, ")\n"))
          )
        )
      ),
      str3("%", prefix, Str.concat("_", Str.fromInt(i + 1)))
    )
  else
    packEnvRev(
      rest,
      env,
      prefix,
      i + 1,
      str4(
        code,
        "  %",
        prefix,
        str4(
          "_",
          Str.fromInt(i + 1),
          " = call ptr @su_list_cons(ptr ",
          str4(v, ", ptr ", cur, ")\n")
        )
      ),
      str3("%", prefix, Str.concat("_", Str.fromInt(i + 1)))
    )

def packEnv(env: List, prefix: String, code: String): List =
  val names = captureNames(env, List.empty)
  if (List.isEmpty(names) == 1) pair(code, "null")
  else
    packEnvRev(
      List.reverse(names),
      env,
      prefix,
      0,
      str4(code, "  %", prefix, "_0 = call ptr @su_list_nil()\n"),
      str3("%", prefix, "_0")
    )

def unpackEnvAt(names: List, prefix: String, i: Int, cur: String, code: String, envAcc: List, outer: List): List =
  if (List.isEmpty(names) == 1) pairSL(code, envAcc)
  else unpackEnvOne(List.head(names), List.tail(names), prefix, i, cur, code, envAcc, outer)

def unpackEnvOne(n: String, rest: List, prefix: String, i: Int, cur: String, code: String, envAcc: List, outer: List): List =
  val k = envGetKind(outer, n)
  val h = str4("%", prefix, "_h", Str.fromInt(i))
  val t = str4("%", prefix, "_t", Str.fromInt(i))
  val code2 = str4(
    code,
    "  ",
    h,
    str4(
      " = call ptr @su_list_head(ptr ",
      cur,
      ")\n  ",
      str4(t, " = call ptr @su_list_tail(ptr ", cur, ")\n")
    )
  )
  if (streq(k, "int") == 1)
    unpackEnvAt(
      rest,
      prefix,
      i + 1,
      t,
      str4(code2, "  %", n, str3(" = call i64 @su_unbox_i64(ptr ", h, ")\n")),
      envPut(envAcc, n, Str.concat("%", n), "int"),
      outer
    )
  else
    unpackEnvAt(rest, prefix, i + 1, t, code2, envPut(envAcc, n, h, k), outer)

def unpackEnv(names: List, prefix: String, outer: List): List =
  if (List.isEmpty(names) == 1) pairSL("", List.empty)
  else unpackEnvAt(names, prefix, 0, "%env", "", List.empty, outer)

def flatMapResultKind(bodyKind: String): String =
  if (isIoKind(bodyKind) == 1) bodyKind
  else if (streq(bodyKind, "int") == 1) "ioi"
  else "io"

def flatMapBind(param: String, payload: String, env: List): List =
  if (streq(param, "_") == 1) pairSL("", env)
  else if (streq(payload, "int") == 1)
    pairSL(
      str3("  %", param, " = call i64 @su_unbox_i64(ptr %value)\n"),
      envPut(env, param, Str.concat("%", param), "int")
    )
  else pairSL("", envPut(env, param, "%value", "ptr"))

def emitHandle(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val contName = Str.concat("su_err_", Str.fromInt(id))
  val names = captureNames(env, List.empty)
  val up = unpackEnv(names, Str.concat("e", Str.fromInt(id)), env)
  val be = emitExpr(nodeExpr(e, 1), strs, defs, sndL(up), Str.concat("e", Str.fromInt(id)), id + 1, conts)
  val wrap = ensureIoPair(sCode(be), sKind(be), sValue(be), Str.concat("e", Str.concat(Str.fromInt(id), "_wrap")))
  val contDef = str4(
    "define internal ptr @",
    contName,
    "(ptr %err, ptr %env) {\nentry:\n",
    str5(fst(up), fst(wrap), "  ret ptr ", snd(wrap), "\n}\n\n")
  )
  val ie = emitExpr(nodeExpr(e, 0), strs, defs, env, Str.concat(prefix, "_he"), sId(be), Str.concat(sConts(be), contDef))
  val ip = ensureIoPair(sCode(ie), sKind(ie), sValue(ie), Str.concat(prefix, "_heio"))
  val packed = packEnv(env, Str.concat(prefix, "_ecap"), fst(ip))
  mkS(
    str4(
      fst(packed),
      "  %",
      prefix,
      str5("_h = call ptr @su_io_handle_error_with(ptr ", snd(ip), ", ptr @", contName, str3(", ptr ", snd(packed), ")\n"))
    ),
    str3("%", prefix, "_h"),
    "io",
    sId(ie),
    sConts(ie)
  )

def emitFlatMap(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val contName = Str.concat("su_cont_", Str.fromInt(id))
  val param = nodeStr(e, 1)
  val inner = emitExpr(nodeExpr(e, 0), strs, defs, env, Str.concat(prefix, "_in"), id + 1, conts)
  val payload = payloadOfKind(sKind(inner))
  val names = captureNames(env, List.empty)
  val up = unpackEnv(names, Str.concat("c", Str.fromInt(id)), env)
  val bind = flatMapBind(param, payload, sndL(up))
  val be = emitExpr(
    nodeExpr(e, 2),
    strs,
    defs,
    sndL(bind),
    Str.concat("c", Str.fromInt(id)),
    sId(inner),
    sConts(inner)
  )
  val wrap = ensureIoPair(sCode(be), sKind(be), sValue(be), Str.concat("c", Str.concat(Str.fromInt(id), "_wrap")))
  val contDef = str4(
    "define internal ptr @",
    contName,
    "(ptr %value, ptr %env) {\nentry:\n",
    str3(fst(up), fst(bind), str4(fst(wrap), "  ret ptr ", snd(wrap), "\n}\n\n"))
  )
  val ip = ensureIoPair(sCode(inner), sKind(inner), sValue(inner), Str.concat(prefix, "_inio"))
  val packed = packEnv(env, Str.concat(prefix, "_cap"), fst(ip))
  mkS(
    str4(
      fst(packed),
      "  %",
      prefix,
      str5("_fm = call ptr @su_io_flatmap(ptr ", snd(ip), ", ptr @", contName, str3(", ptr ", snd(packed), ")\n"))
    ),
    str3("%", prefix, "_fm"),
    flatMapResultKind(sKind(be)),
    sId(be),
    Str.concat(sConts(be), contDef)
  )

def emitCall(strs: List, e: List, defs: List, env: List, prefix: String, id: Int, conts: String): List =
  val callee = nodeStr(e, 0)
  val args = nodeExpr(e, 1)
  val ae = emitArgList(args, strs, defs, env, prefix, 0, id, conts, "", List.empty, List.empty)
  emitCallWithArgs(callee, ae, defs, prefix)

// ae pack: code, id, conts, vals list, kinds list encoded as List:
// List.cons(code, List.cons(Str.fromInt(id), List.cons(conts, List.cons(vals, List.cons(kinds, nil)))))
def emitArgList(args: List, strs: List, defs: List, env: List, prefix: String, i: Int, id: Int, conts: String, code: String, vals: List, kinds: List): List =
  if (List.isEmpty(args) == 1)
    List.cons(code, List.cons(Str.fromInt(id), List.cons(conts, List.cons(List.reverse(vals), List.cons(List.reverse(kinds), List.empty)))))
  else emitArgListOne(List.head(args), List.tail(args), strs, defs, env, prefix, i, id, conts, code, vals, kinds)

def emitArgListOne(arg: List, rest: List, strs: List, defs: List, env: List, prefix: String, i: Int, id: Int, conts: String, code: String, vals: List, kinds: List): List =
  val ee = emitExpr(arg, strs, defs, env, str4(prefix, "_arg", Str.fromInt(i), ""), id, conts)
  emitArgList(
    rest,
    strs,
    defs,
    env,
    prefix,
    i + 1,
    sId(ee),
    sConts(ee),
    Str.concat(code, sCode(ee)),
    List.cons(sValue(ee), vals),
    List.cons(sKind(ee), kinds)
  )

def argPackCode(p: List): String = List.head(p)
def argPackId(p: List): Int = parseInt(List.head(List.tail(p)))
def argPackConts(p: List): String = List.head(List.tail(List.tail(p)))
def argPackVals(p: List): List = List.head(List.tail(List.tail(List.tail(p))))
def argPackKinds(p: List): List = List.head(List.tail(List.tail(List.tail(List.tail(p)))))

def emitCallWithArgs(callee: String, ae: List, defs: List, prefix: String): List =
  emitBuiltinOrUser(callee, argPackCode(ae), argPackVals(ae), argPackKinds(ae), argPackId(ae), argPackConts(ae), defs, prefix)

def emitBuiltinOrUser(callee: String, code: String, vals: List, kinds: List, id: Int, conts: String, defs: List, prefix: String): List =
  if (startsWith(callee, "Str.") == 1) emitBuiltinStr(callee, code, vals, id, conts, prefix)
  else if (startsWith(callee, "List.") == 1) emitBuiltinList(callee, code, vals, kinds, id, conts, prefix)
  else if (startsWith(callee, "Fs.") == 1) emitBuiltinFs(callee, code, vals, id, conts, prefix)
  else if (startsWith(callee, "Sys.") == 1) emitBuiltinSys(callee, code, vals, id, conts, prefix)
  else if (startsWith(callee, "Clock.") == 1) emitBuiltinClock(callee, code, vals, id, conts, prefix)
  else if (startsWith(callee, "Signal.") == 1) emitBuiltinSignal(callee, code, vals, id, conts, prefix)
  else if (startsWith(callee, "View.") == 1) emitBuiltinView(callee, code, vals, id, conts, prefix)
  else if (startsWith(callee, "Todo.") == 1) emitBuiltinTodo(callee, code, vals, id, conts, prefix)
  else if (startsWith(callee, "Ui.") == 1) emitBuiltinUi(callee, code, vals, id, conts, prefix)
  else if (streq(callee, "Random.nextInt") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_random_next_int(i64 ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ioi", id, conts)
  else if (streq(callee, "Net.httpGet") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_net_http_get(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Impurity.runKit") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_impurity_run_kit()\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Effects.runKit") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_effects_run_kit()\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Lexer.classify") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_lexer_classify(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else emitUserCall(callee, code, vals, kinds, id, conts, defs, prefix)

def emitBuiltinStr(callee: String, code: String, vals: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "Str.concat") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_string_concat(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "Str.len") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call i64 @su_string_len(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(callee, "Str.slice") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_string_slice(ptr ", List.at(vals, 0), ", i64 ", List.at(vals, 1), str3(", i64 ", List.at(vals, 2), ")\n"))), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "Str.eq") == 1)
    mkS(
      str5(code, "  %", prefix, str5("_eqi = call i32 @su_string_eq(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n  %"), str4(prefix, "_v = zext i32 %", prefix, "_eqi to i64\n")),
      str3("%", prefix, "_v"),
      "int",
      id,
      conts
    )
  else if (streq(callee, "Str.charAt") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call i64 @su_string_char_at(ptr ", List.at(vals, 0), ", i64 ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(callee, "Str.fromInt") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_string_from_int(i64 ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "Str.indexOf") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call i64 @su_string_index_of(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "int", id, conts)
  else mkS(code, "null", "ptr", id, conts)

def emitBuiltinList(callee: String, code: String, vals: List, kinds: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "List.empty") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_list_nil()\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "List.cons") == 1) emitListCons(code, vals, kinds, id, conts, prefix)
  else if (streq(callee, "List.isEmpty") == 1)
    mkS(
      str5(code, "  %", prefix, str3("_i = call i32 @su_list_is_empty(ptr ", List.at(vals, 0), ")\n  %"), str4(prefix, "_v = zext i32 %", prefix, "_i to i64\n")),
      str3("%", prefix, "_v"),
      "int",
      id,
      conts
    )
  else if (streq(callee, "List.head") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_list_head(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "List.tail") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_list_tail(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "List.len") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call i64 @su_list_len(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(callee, "List.at") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_list_at(ptr ", List.at(vals, 0), ", i64 ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "List.reverse") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_list_reverse(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "List.join") == 1)
    mkS(
      str5(code, "  %", prefix, str3("_sep = call ptr @su_string_cstr(ptr ", List.at(vals, 1), ")\n  %"), str6(prefix, "_v = call ptr @su_list_join(ptr ", List.at(vals, 0), ", ptr %", prefix, "_sep)\n")),
      str3("%", prefix, "_v"),
      "ptr",
      id,
      conts
    )
  else mkS(code, "null", "ptr", id, conts)

def emitBuiltinFs(callee: String, code: String, vals: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "Fs.read") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_fs_read(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Fs.write") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_fs_write(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Fs.list") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_fs_list(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Fs.mkdirs") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_fs_mkdirs(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else mkS(code, "null", "ptr", id, conts)

def emitBuiltinSys(callee: String, code: String, vals: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "Sys.args") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_sys_args()\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Sys.exec") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_sys_exec(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ioi", id, conts)
  else if (streq(callee, "Sys.getenv") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_sys_getenv(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else mkS(code, "null", "ptr", id, conts)

def emitBuiltinClock(callee: String, code: String, vals: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "Clock.realTime") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_clock_real_time()\n")), str3("%", prefix, "_v"), "ioi", id, conts)
  else if (streq(callee, "Clock.monotonic") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_clock_monotonic()\n")), str3("%", prefix, "_v"), "ioi", id, conts)
  else mkS(code, "null", "ptr", id, conts)

def emitBuiltinSignal(callee: String, code: String, vals: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "Signal.int") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_lang_signal_int(i64 ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "Signal.get") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call i64 @su_lang_signal_get(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "int", id, conts)
  else if (streq(callee, "Signal.set") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_lang_signal_set(ptr ", List.at(vals, 0), ", i64 ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "Signal.str") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_lang_signal_str(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else mkS(code, "null", "ptr", id, conts)

def emitBuiltinView(callee: String, code: String, vals: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "View.text") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_lang_view_text(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.textSignal") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_lang_view_text_signal(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.buttonInc") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_lang_view_button_inc(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.buttonSet") == 1)
    mkS(str4(code, "  %", prefix, str4("_v = call ptr @su_lang_view_button_set(ptr ", List.at(vals, 0), str4(", ptr ", List.at(vals, 1), ", i64 ", List.at(vals, 2)), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.column") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_lang_view_column()\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.row") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_lang_view_row()\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.list") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_lang_view_list()\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.scroll") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_lang_view_scroll(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.textField") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_lang_view_text_field(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.icon") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_lang_view_icon(i64 ", List.at(vals, 0), ", i64 ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.image") == 1)
    mkS(
      str4(
        code,
        "  %",
        prefix,
        str4(
          "_v = call ptr @su_lang_view_image(i64 ",
          List.at(vals, 0),
          str4(", i64 ", List.at(vals, 1), ", i64 ", List.at(vals, 2)),
          str3(", ptr ", List.at(vals, 3), ")\n")
        )
      ),
      str3("%", prefix, "_v"),
      "ptr",
      id,
      conts
    )
  else if (streq(callee, "View.addChild") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_lang_view_add_child(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.showWhen") == 1)
    mkS(str4(code, "  %", prefix, str4("_v = call ptr @su_lang_view_show_when(ptr ", List.at(vals, 0), str4(", i64 ", List.at(vals, 1), ", ptr ", List.at(vals, 2)), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.buttonTodoAdd") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_lang_view_button_todo_add(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "View.buttonTodoSave") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_lang_view_button_todo_save(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else mkS(code, "null", "ptr", id, conts)

def emitBuiltinTodo(callee: String, code: String, vals: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "Todo.create") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_lang_todo_create()\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "Todo.load") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_lang_todo_load(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Todo.draft") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_lang_todo_draft(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else if (streq(callee, "Todo.listView") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_lang_todo_list_view(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else mkS(code, "null", "ptr", id, conts)

def emitBuiltinUi(callee: String, code: String, vals: List, id: Int, conts: String, prefix: String): List =
  if (streq(callee, "Ui.run") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_ui_run_view(ptr ", List.at(vals, 0), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Ui.runWithTodo") == 1)
    mkS(str4(code, "  %", prefix, str5("_v = call ptr @su_ui_run_view_todo(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Ui.runHeadless") == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_ui_run_headless_label(ptr ", List.at(vals, 0), ", i32 0, i32 0)\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Ui.runCounter") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_ui_run_counter(i32 0, i32 0)\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Ui.runLive") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_ui_run_live(i32 0, i32 0)\n")), str3("%", prefix, "_v"), "io", id, conts)
  else if (streq(callee, "Ui.runTodo") == 1)
    mkS(str3(code, "  %", Str.concat(prefix, "_v = call ptr @su_ui_run_todo(i32 0, i32 0)\n")), str3("%", prefix, "_v"), "io", id, conts)
  else mkS(code, "null", "ptr", id, conts)

def emitListCons(code: String, vals: List, kinds: List, id: Int, conts: String, prefix: String): List =
  if (streq(List.head(kinds), "int") == 1)
    mkS(
      str4(code, "  %", prefix, str6("_hd = call ptr @su_box_i64(i64 ", List.at(vals, 0), ")\n  %", prefix, "_v = call ptr @su_list_cons(ptr %", str4(prefix, "_hd, ptr ", List.at(vals, 1), ")\n"))),
      str3("%", prefix, "_v"),
      "ptr",
      id,
      conts
    )
  else
    mkS(
      str4(code, "  %", prefix, str5("_v = call ptr @su_list_cons(ptr ", List.at(vals, 0), ", ptr ", List.at(vals, 1), ")\n")),
      str3("%", prefix, "_v"),
      "ptr",
      id,
      conts
    )

def emitUserCall(callee: String, code: String, vals: List, kinds: List, id: Int, conts: String, defs: List, prefix: String): List =
  val d = findDef(defs, callee)
  if (List.isEmpty(d) == 1)
    mkS(str4(code, "  %", prefix, str3("_v = call ptr @su_user_", callee, "()\n")), str3("%", prefix, "_v"), "ptr", id, conts)
  else emitUserCallDef(callee, code, vals, kinds, id, conts, d, prefix)

def emitUserCallDef(callee: String, code: String, vals: List, kinds: List, id: Int, conts: String, d: List, prefix: String): List =
  val retTy = llvmTypeOf(defRet(d))
  val retKind = kindOfType(defRet(d))
  val ap = emitUserArgs(defParams(d), vals, kinds, code, prefix, 0, "")
  mkS(
    str4(fst(ap), "  %", prefix, str5("_v = call ", retTy, " @su_user_", callee, str3("(", snd(ap), ")\n"))),
    str3("%", prefix, "_v"),
    retKind,
    id,
    conts
  )

def emitUserArgs(params: List, vals: List, kinds: List, code: String, prefix: String, i: Int, acc: String): List =
  if (List.isEmpty(params) == 1) pair(code, acc)
  else emitUserArgsOne(List.head(params), List.tail(params), vals, kinds, code, prefix, i, acc)

def emitUserArgsOne(p: String, rest: List, vals: List, kinds: List, code: String, prefix: String, i: Int, acc: String): List =
  val want = kindOfType(paramType(p))
  val got = List.at(kinds, i)
  val v = List.at(vals, i)
  val sep = if (Str.len(acc) == 0) "" else ", "
  if (streq(want, "int") == 1)
    if (streq(got, "int") == 1)
      emitUserArgs(rest, vals, kinds, code, prefix, i + 1, str4(acc, sep, "i64 ", v))
    else
      emitUserArgs(
        rest,
        vals,
        kinds,
        str4(code, "  %", prefix, str3("_a", Str.fromInt(i), " = add i64 0, 0\n")),
        prefix,
        i + 1,
        str4(acc, sep, "i64 %", str3(prefix, "_a", Str.fromInt(i)))
      )
  else if (streq(got, "int") == 1)
    emitUserArgs(
      rest,
      vals,
      kinds,
      str4(code, "  %", prefix, str5("_a", Str.fromInt(i), " = call ptr @su_box_i64(i64 ", v, ")\n")),
      prefix,
      i + 1,
      str4(acc, sep, "ptr %", str3(prefix, "_a", Str.fromInt(i)))
    )
  else emitUserArgs(rest, vals, kinds, code, prefix, i + 1, str4(acc, sep, "ptr ", v))

def emitParams(params: List, i: Int, acc: String): String =
  if (List.isEmpty(params) == 1) acc
  else emitParamsOne(List.head(params), List.tail(params), i, acc)

def emitParamsOne(p: String, rest: List, i: Int, acc: String): String =
  val sep = if (i == 0) "" else ", "
  val part = str4(sep, llvmTypeOf(paramType(p)), " %", paramName(p))
  emitParams(rest, i + 1, Str.concat(acc, part))

def paramsEnv(params: List, env: List): List =
  if (List.isEmpty(params) == 1) env
  else paramsEnv(List.tail(params), envPut(env, paramName(List.head(params)), Str.concat("%", paramName(List.head(params))), kindOfType(paramType(List.head(params)))))

def emitFundefRetInt(code: String, bodyKind: String, bodyVal: String): List =
  if (streq(bodyKind, "int") == 1) pair(str3(code, "  ret i64 ", bodyVal), "")
  else pair(Str.concat(code, "  %ret_coerce = add i64 0, 0\n  ret i64 %ret_coerce"), "")

def emitFundefRetPtr(code: String, bodyKind: String, bodyVal: String): List =
  if (streq(bodyKind, "int") == 1)
    pair(str3(code, "  %ret_box = call ptr @su_box_i64(i64 ", Str.concat(bodyVal, ")\n  ret ptr %ret_box")), "")
  else if (streq(bodyKind, "ptr") == 1) pair(str3(code, "  ret ptr ", bodyVal), "")
  else if (isIoKind(bodyKind) == 1) pair(str3(code, "  ret ptr ", bodyVal), "")
  else pair(Str.concat(code, "  ret ptr null"), "")

def emitFundefRetIo(code: String, bodyKind: String, bodyVal: String): List =
  val p = ensureIoPair(code, bodyKind, bodyVal, "ret_wrap")
  pair(str3(fst(p), "  ret ptr ", snd(p)), "")

def emitFundef(d: List, strs: List, defs: List, id: Int, conts: String): List =
  val ret = llvmTypeOf(defRet(d))
  val env = paramsEnv(defParams(d), List.empty)
  val body = emitExpr(defBody(d), strs, defs, env, "body", id, conts)
  val retKind = kindOfType(defRet(d))
  val tail = if (isIoKind(retKind) == 1) emitFundefRetIo(sCode(body), sKind(body), sValue(body))
    else if (streq(retKind, "int") == 1) emitFundefRetInt(sCode(body), sKind(body), sValue(body))
    else emitFundefRetPtr(sCode(body), sKind(body), sValue(body))
  val ir = str4(
    "define internal ",
    ret,
    " @su_user_",
    str6(defName(d), "(", emitParams(defParams(d), 0, ""), ") {\nentry:\n", fst(tail), "\n}\n\n")
  )
  mkS(ir, "", "", sId(body), sConts(body))

def emitDefs(defs: List, strs: List, allDefs: List, id: Int, conts: String, acc: String): List =
  if (List.isEmpty(defs) == 1) mkS(acc, "", "", id, conts)
  else emitDefsOne(List.head(defs), List.tail(defs), strs, allDefs, id, conts, acc)

def emitDefsOne(d: List, rest: List, strs: List, allDefs: List, id: Int, conts: String, acc: String): List =
  val fe = emitFundef(d, strs, allDefs, id, conts)
  emitDefs(rest, strs, allDefs, sId(fe), sConts(fe), Str.concat(acc, sCode(fe)))

def emitMain(prog: List, strs: List, defsIr: List): String =
  val body = emitExpr(progMain(prog), strs, progDefs(prog), List.empty, "build", sId(defsIr), sConts(defsIr))
  val p = ensureIoPair(sCode(body), sKind(body), sValue(body), "wrapped")
  val mainFn = str4(
    "define i32 @main(i32 %argc, ptr %argv) {\nentry:\n",
    fst(p),
    "  %rc = call i32 @su_runtime_main_args(ptr ",
    Str.concat(snd(p), ", i32 %argc, ptr %argv)\n  ret i32 %rc\n}\n")
  )
  str4(sConts(body), sCode(defsIr), mainFn, "")

def emitProgram(prog: List): String =
  val strs = collectProgram(prog)
  val header = emitHeader(strs)
  val defsIr = emitDefs(progDefs(prog), strs, progDefs(prog), 0, "", "")
  Str.concat(header, emitMain(prog, strs, defsIr))
