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

e return yy::ExpressionParser::make_Numeric(yytext, loc);
pi return yy::ExpressionParser::make_Numeric(yytext, loc);
[0-9]+ return yy::ExpressionParser::make_Numeric(yytext, loc);

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
