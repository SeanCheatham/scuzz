package scalui.compiler

def lowerProg(prog: List): List =
  List.cons("Prog", List.cons(nodeStr(prog, 0), List.cons(progEnums(prog), List.cons(lowerDefs(progDefs(prog), List.empty()), List.cons(lowerExpr(progMain(prog)), List.empty())))))

def lowerDefs(defs: List, acc: List): List =
  if (List.isEmpty(defs) == 1) List.reverse(acc) else lowerDefs(List.tail(defs), List.cons(lowerDef(List.head(defs)), acc))

def lowerDef(d: List): List =
  List.cons("Def", List.cons(nodeStr(d, 0), List.cons(nodeExpr(d, 1), List.cons(nodeStr(d, 2), List.cons(lowerExpr(nodeExpr(d, 3)), List.empty())))))

def lowerExpr(e: List): List =
  lowerExprTag(exprTag(e), e)

def lowerExprTag(tag: String, e: List): List =
  if (streq(tag, "For") == 1) desugarFor(nodeExpr(e, 0), lowerExpr(nodeExpr(e, 1))) else if (streq(tag, "Let") == 1) List.cons("Let", List.cons(nodeStr(e, 0), List.cons(lowerExpr(nodeExpr(e, 1)), List.cons(lowerExpr(nodeExpr(e, 2)), List.empty())))) else if (streq(tag, "FlatMap") == 1) List.cons("FlatMap", List.cons(lowerExpr(nodeExpr(e, 0)), List.cons(nodeStr(e, 1), List.cons(lowerExpr(nodeExpr(e, 2)), List.empty())))) else if (streq(tag, "Handle") == 1) List.cons("Handle", List.cons(lowerExpr(nodeExpr(e, 0)), List.cons(lowerExpr(nodeExpr(e, 1)), List.empty()))) else if (streq(tag, "Attempt") == 1) List.cons("Attempt", List.cons(lowerExpr(nodeExpr(e, 0)), List.empty())) else if (streq(tag, "If") == 1) List.cons("If", List.cons(lowerExpr(nodeExpr(e, 0)), List.cons(lowerExpr(nodeExpr(e, 1)), List.cons(lowerExpr(nodeExpr(e, 2)), List.empty())))) else if (streq(tag, "BinOp") == 1) List.cons("BinOp", List.cons(nodeStr(e, 0), List.cons(lowerExpr(nodeExpr(e, 1)), List.cons(lowerExpr(nodeExpr(e, 2)), List.empty())))) else if (streq(tag, "Call") == 1) List.cons("Call", List.cons(nodeStr(e, 0), List.cons(lowerExprList(nodeExpr(e, 1), List.empty()), List.empty()))) else if (streq(tag, "Lambda") == 1) List.cons("Lambda", List.cons(nodeStr(e, 0), List.cons(lowerExpr(nodeExpr(e, 1)), List.empty()))) else if (streq(tag, "Match") == 1) List.cons("Match", List.cons(lowerExpr(nodeExpr(e, 0)), List.cons(lowerArms(nodeExpr(e, 1), List.empty()), List.empty()))) else if (streq(tag, "Println") == 1) List.cons("Println", List.cons(lowerExpr(nodeExpr(e, 0)), List.empty())) else if (streq(tag, "Fail") == 1) List.cons("Fail", List.cons(lowerExpr(nodeExpr(e, 0)), List.empty())) else if (streq(tag, "Pure") == 1) List.cons("Pure", List.cons(lowerExpr(nodeExpr(e, 0)), List.empty())) else if (streq(tag, "Sleep") == 1) List.cons("Sleep", List.cons(lowerExpr(nodeExpr(e, 0)), List.empty())) else if (streq(tag, "IoRace") == 1) List.cons("IoRace", List.cons(lowerExpr(nodeExpr(e, 0)), List.cons(lowerExpr(nodeExpr(e, 1)), List.empty()))) else if (streq(tag, "IoBoth") == 1) List.cons("IoBoth", List.cons(lowerExpr(nodeExpr(e, 0)), List.cons(lowerExpr(nodeExpr(e, 1)), List.empty()))) else if (streq(tag, "ListLit") == 1) List.cons("ListLit", List.cons(lowerExprList(nodeExpr(e, 0), List.empty()), List.empty())) else if (streq(tag, "Interp") == 1) List.cons("Interp", List.cons(lowerInterpParts(nodeExpr(e, 0), List.empty()), List.empty())) else e

def lowerExprList(xs: List, acc: List): List =
  if (List.isEmpty(xs) == 1) List.reverse(acc) else lowerExprList(List.tail(xs), List.cons(lowerExpr(List.head(xs)), acc))

def lowerArms(arms: List, acc: List): List =
  if (List.isEmpty(arms) == 1) List.reverse(acc) else lowerArms(List.tail(arms), List.cons(lowerArm(List.head(arms)), acc))

def lowerArm(arm: List): List =
  List.cons("Arm", List.cons(nodeExpr(arm, 0), List.cons(lowerExpr(nodeExpr(arm, 1)), List.empty())))

def lowerInterpParts(parts: List, acc: List): List =
  if (List.isEmpty(parts) == 1) List.reverse(acc) else lowerInterpParts(List.tail(parts), List.cons(lowerInterpPart(List.head(parts)), acc))

def lowerInterpPart(part: List): List =
  if (streq(List.head(part), "Lit") == 1) part else List.cons("Hole", List.cons(lowerExpr(nodeExpr(part, 0)), List.empty()))

def forBindersHaveDraw(binders: List): Int =
  if (List.isEmpty(binders) == 1) 0 else if (streq(List.head(List.head(binders)), "Draw") == 1) 1 else forBindersHaveDraw(List.tail(binders))

def desugarFor(binders: List, body: List): List =
  if (forBindersHaveDraw(binders) == 1) desugarForBinders(binders, List.cons("Pure", List.cons(body, List.empty()))) else desugarForBinders(binders, body)

def desugarForBinders(binders: List, body: List): List =
  if (List.isEmpty(binders) == 1) body else wrapForBinder(List.head(binders), desugarForBinders(List.tail(binders), body))

def wrapForBinder(b: List, body: List): List =
  if (streq(List.head(b), "Eq") == 1) List.cons("Let", List.cons(nodeStr(b, 0), List.cons(lowerExpr(nodeExpr(b, 1)), List.cons(body, List.empty())))) else List.cons("FlatMap", List.cons(lowerExpr(nodeExpr(b, 1)), List.cons(nodeStr(b, 0), List.cons(body, List.empty()))))
