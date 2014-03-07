%skeleton "lalr1.cc"
%require "3.0.2"

%defines
%define parser_class_name {ExpressionParser}
%define api.token.constructor
%define api.value.type variant
%define parse.assert
%define parse.trace
%define parse.error verbose

%code requires
{
#include "expr.h"

namespace expression {
class ParseContext;
}  // namespace expression
}

%param { expression::ParseContext *context }

%locations
%code
{
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <cstdio>
#include <cstring>

#include "expr-parse.h"
}

%token
  END  0     "end of file"
  LPAREN     "("
  RPAREN     ")"
  STAR       "*"
  PLUS       "+"
  COMMA      ","
  MINUS      "-"
  SLASH      "/"
  PERCENT    "%"
  CIRCUMFLEX "^"
  COS        "COS"
  SIN        "SIN"
  LOG        "LOG"
  HEX        "HEX"
  ;
%token INVALID

%token <std::string> Identifier "Identifier"
%token <std::string> Numeric "Numeric"

%type <expression::Expression*> expression

%%
%start document;
document
    : expression END
      {
        context->expression_.reset($1);
      }
    ;

%left "+" "-";
%left "*" "/" "%";
%left UMINUS;

expression
    : "Numeric"
      {
        $$ = expression::Expression::CreateNumeric($1);
      }
    | "(" expression ")"
      {
        std::swap($$, $2);
      }
    | "-" expression %prec UMINUS
      {
        $$ = new expression::Expression(expression::Expression::kMinus, $2);
      }
    | expression "+" expression
      {
        $$ = new expression::Expression(expression::Expression::kAdd, $1, $3);
      }
    | expression "-" expression
      {
        $$ = new expression::Expression(expression::Expression::kSubtract, $1, $3);
      }
    | expression "*" expression
      {
        $$ = new expression::Expression(expression::Expression::kMultiply, $1, $3);
      }
    | expression "/" expression
      {
        $$ = new expression::Expression(expression::Expression::kDivide, $1, $3);
      }
    | expression "%" expression
      {
        $$ = new expression::Expression(expression::Expression::kModulus, $1, $3);
      }
    | expression "^" expression
      {
        $$ = new expression::Expression(expression::Expression::kExponentiate, $1, $3);
      }
    | expression "*" "*" expression
      {
        $$ = new expression::Expression(expression::Expression::kExponentiate, $1, $4);
      }
    | "COS" "(" expression ")"
      {
        $$ = new expression::Expression(expression::Expression::kCos, $3);
      }
    | "SIN" "(" expression ")"
      {
        $$ = new expression::Expression(expression::Expression::kSin, $3);
      }
    | "LOG" "(" expression ")"
      {
        $$ = new expression::Expression(expression::Expression::kLog, $3);
      }
    | "HEX" "(" expression ")"
      {
        $$ = new expression::Expression(expression::Expression::kHex, $3);
      }
    ;
%%
void yy::ExpressionParser::error(const location_type& l, const std::string& m) {
}
