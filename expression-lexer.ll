%{
#include <cctype>
#include <cstring>

#include "expr-parse.h"

static yy::location loc;
%}
%option noyywrap
%option nounput
%option batch
%option noinput
%option never-interactive
blank [ \t\r]
%{
# define YY_USER_ACTION loc.columns(yyleng);
%}
%%
%{
loc.step();
%}
"(" return yy::ExpressionParser::make_LPAREN(loc);
")" return yy::ExpressionParser::make_RPAREN(loc);
"*" return yy::ExpressionParser::make_STAR(loc);
"+" return yy::ExpressionParser::make_PLUS(loc);
"," return yy::ExpressionParser::make_COMMA(loc);
"-" return yy::ExpressionParser::make_MINUS(loc);
"/" return yy::ExpressionParser::make_SLASH(loc);
"%" return yy::ExpressionParser::make_PERCENT(loc);
"^" return yy::ExpressionParser::make_CIRCUMFLEX(loc);

[0-9][0-9][0-9][0-9]-[01][0123456789]-[0123][0123456789](\ [012][0123456789]:[012345][0123456789](:[012345][0123456789])?)? return yy::ExpressionParser::make_Time(yytext, loc);
[012][0123456789]:[012345][0123456789](:[012345][0123456789])? return yy::ExpressionParser::make_Time(yytext, loc);
now    return yy::ExpressionParser::make_Time(yytext, loc);
e      return yy::ExpressionParser::make_Numeric(yytext, loc);
pi     return yy::ExpressionParser::make_Numeric(yytext, loc);
0x[0-9a-fA-F]+ return yy::ExpressionParser::make_Numeric(yytext, loc);
[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)? return yy::ExpressionParser::make_Numeric(yytext, loc);

cos    return yy::ExpressionParser::make_COS(loc);
sin    return yy::ExpressionParser::make_SIN(loc);
log    return yy::ExpressionParser::make_LOG(loc);
hex    return yy::ExpressionParser::make_HEX(loc);

[A-Za-z_][A-Za-z0-9_.]*  return yy::ExpressionParser::make_Identifier(yytext, loc);
{blank}+                 loc.step();
[\n]+                    loc.lines(yyleng); loc.step();
.                        { context->SetError(loc, "Invalid character"); return yy::ExpressionParser::make_INVALID(loc); }
<<EOF>>                  return yy::ExpressionParser::make_END(loc);
%%

namespace expression {

Expression* ParseContext::ParseExpression(const std::string& expression) {
  error_.clear();

  yy::ExpressionParser parser(this);
  FILE* buffer = fmemopen(const_cast<char*>(expression.data()), expression.size(), "r");
  YY_FLUSH_BUFFER;
  yyin = buffer;

  parser.parse();

  fclose(buffer);

  if (!error_.empty())
    return nullptr;

  return expression_.release();
}

}  // namespace expression
