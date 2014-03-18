// A Bison parser, made by GNU Bison 3.0.2.

// Skeleton implementation for Bison LALR(1) parsers in C++

// Copyright (C) 2002-2013 Free Software Foundation, Inc.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// As a special exception, you may create a larger work that contains
// part or all of the Bison parser skeleton and distribute that work
// under terms of your choice, so long as that work isn't itself a
// parser generator using the skeleton or a modified version thereof
// as a parser skeleton.  Alternatively, if you modify or redistribute
// the parser skeleton itself, you may (at your option) remove this
// special exception, which will cause the skeleton and the resulting
// Bison output files to be licensed under the GNU General Public
// License without this special exception.

// This special exception was added by the Free Software Foundation in
// version 2.2 of Bison.


// First part of user declarations.

#line 37 "expression-parser.cc" // lalr1.cc:399

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

#include "expression-parser.hh"

// User implementation prologue.

#line 51 "expression-parser.cc" // lalr1.cc:407
// Unqualified %code blocks.
#line 25 "expression-parser.yy" // lalr1.cc:408

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <cstdio>
#include <cstring>

#include "expr-parse.h"

#line 64 "expression-parser.cc" // lalr1.cc:408


#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> // FIXME: INFRINGES ON USER NAME SPACE.
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K].location)
/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

# ifndef YYLLOC_DEFAULT
#  define YYLLOC_DEFAULT(Current, Rhs, N)                               \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).begin  = YYRHSLOC (Rhs, 1).begin;                   \
          (Current).end    = YYRHSLOC (Rhs, N).end;                     \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).begin = (Current).end = YYRHSLOC (Rhs, 0).end;      \
        }                                                               \
    while (/*CONSTCOND*/ false)
# endif


// Suppress unused-variable warnings by "using" E.
#define YYUSE(E) ((void) (E))

// Enable debugging if requested.
#if YYDEBUG

// A pseudo ostream that takes yydebug_ into account.
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Symbol)         \
  do {                                          \
    if (yydebug_)                               \
    {                                           \
      *yycdebug_ << Title << ' ';               \
      yy_print_ (*yycdebug_, Symbol);           \
      *yycdebug_ << std::endl;                  \
    }                                           \
  } while (false)

# define YY_REDUCE_PRINT(Rule)          \
  do {                                  \
    if (yydebug_)                       \
      yy_reduce_print_ (Rule);          \
  } while (false)

# define YY_STACK_PRINT()               \
  do {                                  \
    if (yydebug_)                       \
      yystack_print_ ();                \
  } while (false)

#else // !YYDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YYUSE(Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void>(0)
# define YY_STACK_PRINT()                static_cast<void>(0)

#endif // !YYDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyempty = true)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)


namespace yy {
#line 150 "expression-parser.cc" // lalr1.cc:474

  /* Return YYSTR after stripping away unnecessary quotes and
     backslashes, so that it's suitable for yyerror.  The heuristic is
     that double-quoting is unnecessary unless the string contains an
     apostrophe, a comma, or backslash (other than backslash-backslash).
     YYSTR is taken from yytname.  */
  std::string
  ExpressionParser::yytnamerr_ (const char *yystr)
  {
    if (*yystr == '"')
      {
        std::string yyr = "";
        char const *yyp = yystr;

        for (;;)
          switch (*++yyp)
            {
            case '\'':
            case ',':
              goto do_not_strip_quotes;

            case '\\':
              if (*++yyp != '\\')
                goto do_not_strip_quotes;
              // Fall through.
            default:
              yyr += *yyp;
              break;

            case '"':
              return yyr;
            }
      do_not_strip_quotes: ;
      }

    return yystr;
  }


  /// Build a parser object.
  ExpressionParser::ExpressionParser (expression::ParseContext *context_yyarg)
    :
#if YYDEBUG
      yydebug_ (false),
      yycdebug_ (&std::cerr),
#endif
      context (context_yyarg)
  {}

  ExpressionParser::~ExpressionParser ()
  {}


  /*---------------.
  | Symbol types.  |
  `---------------*/



  // by_state.
  inline
  ExpressionParser::by_state::by_state ()
    : state (empty)
  {}

  inline
  ExpressionParser::by_state::by_state (const by_state& other)
    : state (other.state)
  {}

  inline
  void
  ExpressionParser::by_state::move (by_state& that)
  {
    state = that.state;
    that.state = empty;
  }

  inline
  ExpressionParser::by_state::by_state (state_type s)
    : state (s)
  {}

  inline
  ExpressionParser::symbol_number_type
  ExpressionParser::by_state::type_get () const
  {
    return state == empty ? 0 : yystos_[state];
  }

  inline
  ExpressionParser::stack_symbol_type::stack_symbol_type ()
  {}


  inline
  ExpressionParser::stack_symbol_type::stack_symbol_type (state_type s, symbol_type& that)
    : super_type (s, that.location)
  {
      switch (that.type_get ())
    {
      case 22: // expression
        value.move< expression::Expression* > (that.value);
        break;

      case 17: // "Identifier"
      case 18: // "Numeric"
        value.move< std::string > (that.value);
        break;

      default:
        break;
    }

    // that is emptied.
    that.type = empty;
  }

  inline
  ExpressionParser::stack_symbol_type&
  ExpressionParser::stack_symbol_type::operator= (const stack_symbol_type& that)
  {
    state = that.state;
      switch (that.type_get ())
    {
      case 22: // expression
        value.copy< expression::Expression* > (that.value);
        break;

      case 17: // "Identifier"
      case 18: // "Numeric"
        value.copy< std::string > (that.value);
        break;

      default:
        break;
    }

    location = that.location;
    return *this;
  }


  template <typename Base>
  inline
  void
  ExpressionParser::yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const
  {
    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yysym);
  }

#if YYDEBUG
  template <typename Base>
  void
  ExpressionParser::yy_print_ (std::ostream& yyo,
                                     const basic_symbol<Base>& yysym) const
  {
    std::ostream& yyoutput = yyo;
    YYUSE (yyoutput);
    symbol_number_type yytype = yysym.type_get ();
    yyo << (yytype < yyntokens_ ? "token" : "nterm")
        << ' ' << yytname_[yytype] << " ("
        << yysym.location << ": ";
    YYUSE (yytype);
    yyo << ')';
  }
#endif

  inline
  void
  ExpressionParser::yypush_ (const char* m, state_type s, symbol_type& sym)
  {
    stack_symbol_type t (s, sym);
    yypush_ (m, t);
  }

  inline
  void
  ExpressionParser::yypush_ (const char* m, stack_symbol_type& s)
  {
    if (m)
      YY_SYMBOL_PRINT (m, s);
    yystack_.push (s);
  }

  inline
  void
  ExpressionParser::yypop_ (unsigned int n)
  {
    yystack_.pop (n);
  }

#if YYDEBUG
  std::ostream&
  ExpressionParser::debug_stream () const
  {
    return *yycdebug_;
  }

  void
  ExpressionParser::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


  ExpressionParser::debug_level_type
  ExpressionParser::debug_level () const
  {
    return yydebug_;
  }

  void
  ExpressionParser::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif // YYDEBUG

  inline ExpressionParser::state_type
  ExpressionParser::yy_lr_goto_state_ (state_type yystate, int yysym)
  {
    int yyr = yypgoto_[yysym - yyntokens_] + yystate;
    if (0 <= yyr && yyr <= yylast_ && yycheck_[yyr] == yystate)
      return yytable_[yyr];
    else
      return yydefgoto_[yysym - yyntokens_];
  }

  inline bool
  ExpressionParser::yy_pact_value_is_default_ (int yyvalue)
  {
    return yyvalue == yypact_ninf_;
  }

  inline bool
  ExpressionParser::yy_table_value_is_error_ (int yyvalue)
  {
    return yyvalue == yytable_ninf_;
  }

  int
  ExpressionParser::parse ()
  {
    /// Whether yyla contains a lookahead.
    bool yyempty = true;

    // State.
    int yyn;
    /// Length of the RHS of the rule being reduced.
    int yylen = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// The lookahead symbol.
    symbol_type yyla;

    /// The locations where the error started and ended.
    stack_symbol_type yyerror_range[3];

    /// The return value of parse ().
    int yyresult;

    // FIXME: This shoud be completely indented.  It is not yet to
    // avoid gratuitous conflicts when merging into the master branch.
    try
      {
    YYCDEBUG << "Starting parse" << std::endl;


    /* Initialize the stack.  The initial state will be set in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystack_.clear ();
    yypush_ (YY_NULLPTR, 0, yyla);

    // A new symbol was pushed on the stack.
  yynewstate:
    YYCDEBUG << "Entering state " << yystack_[0].state << std::endl;

    // Accept?
    if (yystack_[0].state == yyfinal_)
      goto yyacceptlab;

    goto yybackup;

    // Backup.
  yybackup:

    // Try to take a decision without lookahead.
    yyn = yypact_[yystack_[0].state];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    // Read a lookahead token.
    if (yyempty)
      {
        YYCDEBUG << "Reading a token: ";
        try
          {
            symbol_type yylookahead (yylex (context));
            yyla.move (yylookahead);
          }
        catch (const syntax_error& yyexc)
          {
            error (yyexc);
            goto yyerrlab1;
          }
        yyempty = false;
      }
    YY_SYMBOL_PRINT ("Next token is", yyla);

    /* If the proper action on seeing token YYLA.TYPE is to reduce or
       to detect an error, take that action.  */
    yyn += yyla.type_get ();
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yyla.type_get ())
      goto yydefault;

    // Reduce or error.
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
        if (yy_table_value_is_error_ (yyn))
          goto yyerrlab;
        yyn = -yyn;
        goto yyreduce;
      }

    // Discard the token being shifted.
    yyempty = true;

    // Count tokens shifted since error; after three, turn off error status.
    if (yyerrstatus_)
      --yyerrstatus_;

    // Shift the lookahead token.
    yypush_ ("Shifting", yyn, yyla);
    goto yynewstate;

  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[yystack_[0].state];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;

  /*-----------------------------.
  | yyreduce -- Do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    {
      stack_symbol_type yylhs;
      yylhs.state = yy_lr_goto_state_(yystack_[yylen].state, yyr1_[yyn]);
      /* Variants are always initialized to an empty instance of the
         correct type. The default '$$ = $1' action is NOT applied
         when using variants.  */
        switch (yyr1_[yyn])
    {
      case 22: // expression
        yylhs.value.build< expression::Expression* > ();
        break;

      case 17: // "Identifier"
      case 18: // "Numeric"
        yylhs.value.build< std::string > ();
        break;

      default:
        break;
    }


      // Compute the default @$.
      {
        slice<stack_symbol_type, stack_type> slice (yystack_, yylen);
        YYLLOC_DEFAULT (yylhs.location, slice, yylen);
      }

      // Perform the reduction.
      YY_REDUCE_PRINT (yyn);
      try
        {
          switch (yyn)
            {
  case 2:
#line 63 "expression-parser.yy" // lalr1.cc:847
    {
        context->expression_.reset(yystack_[1].value.as< expression::Expression* > ());
      }
#line 547 "expression-parser.cc" // lalr1.cc:847
    break;

  case 3:
#line 74 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = expression::Expression::CreateNumeric(yystack_[0].value.as< std::string > ());
      }
#line 555 "expression-parser.cc" // lalr1.cc:847
    break;

  case 4:
#line 78 "expression-parser.yy" // lalr1.cc:847
    {
        std::swap(yylhs.value.as< expression::Expression* > (), yystack_[1].value.as< expression::Expression* > ());
      }
#line 563 "expression-parser.cc" // lalr1.cc:847
    break;

  case 5:
#line 82 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kMinus, yystack_[0].value.as< expression::Expression* > ());
      }
#line 571 "expression-parser.cc" // lalr1.cc:847
    break;

  case 6:
#line 86 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kAdd, yystack_[2].value.as< expression::Expression* > (), yystack_[0].value.as< expression::Expression* > ());
      }
#line 579 "expression-parser.cc" // lalr1.cc:847
    break;

  case 7:
#line 90 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kSubtract, yystack_[2].value.as< expression::Expression* > (), yystack_[0].value.as< expression::Expression* > ());
      }
#line 587 "expression-parser.cc" // lalr1.cc:847
    break;

  case 8:
#line 94 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kMultiply, yystack_[2].value.as< expression::Expression* > (), yystack_[0].value.as< expression::Expression* > ());
      }
#line 595 "expression-parser.cc" // lalr1.cc:847
    break;

  case 9:
#line 98 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kDivide, yystack_[2].value.as< expression::Expression* > (), yystack_[0].value.as< expression::Expression* > ());
      }
#line 603 "expression-parser.cc" // lalr1.cc:847
    break;

  case 10:
#line 102 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kModulus, yystack_[2].value.as< expression::Expression* > (), yystack_[0].value.as< expression::Expression* > ());
      }
#line 611 "expression-parser.cc" // lalr1.cc:847
    break;

  case 11:
#line 106 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kExponentiate, yystack_[2].value.as< expression::Expression* > (), yystack_[0].value.as< expression::Expression* > ());
      }
#line 619 "expression-parser.cc" // lalr1.cc:847
    break;

  case 12:
#line 110 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kExponentiate, yystack_[3].value.as< expression::Expression* > (), yystack_[0].value.as< expression::Expression* > ());
      }
#line 627 "expression-parser.cc" // lalr1.cc:847
    break;

  case 13:
#line 114 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kCos, yystack_[1].value.as< expression::Expression* > ());
      }
#line 635 "expression-parser.cc" // lalr1.cc:847
    break;

  case 14:
#line 118 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kSin, yystack_[1].value.as< expression::Expression* > ());
      }
#line 643 "expression-parser.cc" // lalr1.cc:847
    break;

  case 15:
#line 122 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kLog, yystack_[1].value.as< expression::Expression* > ());
      }
#line 651 "expression-parser.cc" // lalr1.cc:847
    break;

  case 16:
#line 126 "expression-parser.yy" // lalr1.cc:847
    {
        yylhs.value.as< expression::Expression* > () = new expression::Expression(expression::Expression::kHex, yystack_[1].value.as< expression::Expression* > ());
      }
#line 659 "expression-parser.cc" // lalr1.cc:847
    break;


#line 663 "expression-parser.cc" // lalr1.cc:847
            default:
              break;
            }
        }
      catch (const syntax_error& yyexc)
        {
          error (yyexc);
          YYERROR;
        }
      YY_SYMBOL_PRINT ("-> $$ =", yylhs);
      yypop_ (yylen);
      yylen = 0;
      YY_STACK_PRINT ();

      // Shift the result of the reduction.
      yypush_ (YY_NULLPTR, yylhs);
    }
    goto yynewstate;

  /*--------------------------------------.
  | yyerrlab -- here on detecting error.  |
  `--------------------------------------*/
  yyerrlab:
    // If not already recovering from an error, report this error.
    if (!yyerrstatus_)
      {
        ++yynerrs_;
        error (yyla.location, yysyntax_error_ (yystack_[0].state,
                                           yyempty ? yyempty_ : yyla.type_get ()));
      }


    yyerror_range[1].location = yyla.location;
    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */

        // Return failure if at end of input.
        if (yyla.type_get () == yyeof_)
          YYABORT;
        else if (!yyempty)
          {
            yy_destroy_ ("Error: discarding", yyla);
            yyempty = true;
          }
      }

    // Else will try to reuse lookahead token after shifting the error token.
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:

    /* Pacify compilers like GCC when the user code never invokes
       YYERROR and the label yyerrorlab therefore never appears in user
       code.  */
    if (false)
      goto yyerrorlab;
    yyerror_range[1].location = yystack_[yylen - 1].location;
    /* Do not reclaim the symbols of the rule whose action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    goto yyerrlab1;

  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;   // Each real token shifted decrements this.
    {
      stack_symbol_type error_token;
      for (;;)
        {
          yyn = yypact_[yystack_[0].state];
          if (!yy_pact_value_is_default_ (yyn))
            {
              yyn += yyterror_;
              if (0 <= yyn && yyn <= yylast_ && yycheck_[yyn] == yyterror_)
                {
                  yyn = yytable_[yyn];
                  if (0 < yyn)
                    break;
                }
            }

          // Pop the current state because it cannot handle the error token.
          if (yystack_.size () == 1)
            YYABORT;

          yyerror_range[1].location = yystack_[0].location;
          yy_destroy_ ("Error: popping", yystack_[0]);
          yypop_ ();
          YY_STACK_PRINT ();
        }

      yyerror_range[2].location = yyla.location;
      YYLLOC_DEFAULT (error_token.location, yyerror_range, 2);

      // Shift the error token.
      error_token.state = yyn;
      yypush_ ("Shifting", error_token);
    }
    goto yynewstate;

    // Accept.
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;

    // Abort.
  yyabortlab:
    yyresult = 1;
    goto yyreturn;

  yyreturn:
    if (!yyempty)
      yy_destroy_ ("Cleanup: discarding lookahead", yyla);

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    while (1 < yystack_.size ())
      {
        yy_destroy_ ("Cleanup: popping", yystack_[0]);
        yypop_ ();
      }

    return yyresult;
  }
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack"
                 << std::endl;
        // Do not try to display the values of the reclaimed symbols,
        // as their printer might throw an exception.
        if (!yyempty)
          yy_destroy_ (YY_NULLPTR, yyla);

        while (1 < yystack_.size ())
          {
            yy_destroy_ (YY_NULLPTR, yystack_[0]);
            yypop_ ();
          }
        throw;
      }
  }

  void
  ExpressionParser::error (const syntax_error& yyexc)
  {
    error (yyexc.location, yyexc.what());
  }

  // Generate an error message.
  std::string
  ExpressionParser::yysyntax_error_ (state_type yystate, symbol_number_type yytoken) const
  {
    std::string yyres;
    // Number of reported tokens (one for the "unexpected", one per
    // "expected").
    size_t yycount = 0;
    // Its maximum.
    enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
    // Arguments of yyformat.
    char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];

    /* There are many possibilities here to consider:
       - If this state is a consistent state with a default action, then
         the only way this function was invoked is if the default action
         is an error action.  In that case, don't check for expected
         tokens because there are none.
       - The only way there can be no lookahead present (in yytoken) is
         if this state is a consistent state with a default action.
         Thus, detecting the absence of a lookahead is sufficient to
         determine that there is no unexpected or expected token to
         report.  In that case, just report a simple "syntax error".
       - Don't assume there isn't a lookahead just because this state is
         a consistent state with a default action.  There might have
         been a previous inconsistent state, consistent state with a
         non-default action, or user semantic action that manipulated
         yyla.  (However, yyla is currently not documented for users.)
       - Of course, the expected token list depends on states to have
         correct lookahead information, and it depends on the parser not
         to perform extra reductions after fetching a lookahead from the
         scanner and before detecting a syntax error.  Thus, state
         merging (from LALR or IELR) and default reductions corrupt the
         expected token list.  However, the list is correct for
         canonical LR with one exception: it will still contain any
         token that will not be accepted due to an error action in a
         later state.
    */
    if (yytoken != yyempty_)
      {
        yyarg[yycount++] = yytname_[yytoken];
        int yyn = yypact_[yystate];
        if (!yy_pact_value_is_default_ (yyn))
          {
            /* Start YYX at -YYN if negative to avoid negative indexes in
               YYCHECK.  In other words, skip the first -YYN actions for
               this state because they are default actions.  */
            int yyxbegin = yyn < 0 ? -yyn : 0;
            // Stay within bounds of both yycheck and yytname.
            int yychecklim = yylast_ - yyn + 1;
            int yyxend = yychecklim < yyntokens_ ? yychecklim : yyntokens_;
            for (int yyx = yyxbegin; yyx < yyxend; ++yyx)
              if (yycheck_[yyx + yyn] == yyx && yyx != yyterror_
                  && !yy_table_value_is_error_ (yytable_[yyx + yyn]))
                {
                  if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                    {
                      yycount = 1;
                      break;
                    }
                  else
                    yyarg[yycount++] = yytname_[yyx];
                }
          }
      }

    char const* yyformat = YY_NULLPTR;
    switch (yycount)
      {
#define YYCASE_(N, S)                         \
        case N:                               \
          yyformat = S;                       \
        break
        YYCASE_(0, YY_("syntax error"));
        YYCASE_(1, YY_("syntax error, unexpected %s"));
        YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
        YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
        YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
        YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
      }

    // Argument number.
    size_t yyi = 0;
    for (char const* yyp = yyformat; *yyp; ++yyp)
      if (yyp[0] == '%' && yyp[1] == 's' && yyi < yycount)
        {
          yyres += yytnamerr_ (yyarg[yyi++]);
          ++yyp;
        }
      else
        yyres += *yyp;
    return yyres;
  }


  const signed char ExpressionParser::yypact_ninf_ = -3;

  const signed char ExpressionParser::yytable_ninf_ = -1;

  const signed char
  ExpressionParser::yypact_[] =
  {
      29,    29,    29,     2,     7,    12,    13,    -3,    23,    40,
      -2,    14,    29,    29,    29,    29,    -3,    -3,    21,    29,
      29,    29,    29,    29,    -3,    48,    56,    64,    72,    29,
      14,    86,    86,    14,    14,    79,    -3,    -3,    -3,    -3,
      14
  };

  const unsigned char
  ExpressionParser::yydefact_[] =
  {
       0,     0,     0,     0,     0,     0,     0,     3,     0,     0,
       0,     5,     0,     0,     0,     0,     1,     2,     0,     0,
       0,     0,     0,     0,     4,     0,     0,     0,     0,     0,
       8,     6,     7,     9,    10,    11,    13,    14,    15,    16,
      12
  };

  const signed char
  ExpressionParser::yypgoto_[] =
  {
      -3,    -3,    -1
  };

  const signed char
  ExpressionParser::yydefgoto_[] =
  {
      -1,     8,     9
  };

  const unsigned char
  ExpressionParser::yytable_[] =
  {
      10,    11,    24,    18,    19,    12,    20,    21,    22,    23,
      13,    25,    26,    27,    28,    14,    15,    30,    31,    32,
      33,    34,    35,    16,     1,    23,    29,     0,    40,     2,
       0,     0,     1,     3,     4,     5,     6,     2,     0,     7,
      17,     3,     4,     5,     6,    18,    19,     7,    20,    21,
      22,    23,    36,    18,    19,     0,    20,    21,    22,    23,
      37,    18,    19,     0,    20,    21,    22,    23,    38,    18,
      19,     0,    20,    21,    22,    23,    39,    18,    19,     0,
      20,    21,    22,    23,    18,    19,     0,    20,    21,    22,
      23,    18,     0,     0,     0,    21,    22,    23
  };

  const signed char
  ExpressionParser::yycheck_[] =
  {
       1,     2,     4,     5,     6,     3,     8,     9,    10,    11,
       3,    12,    13,    14,    15,     3,     3,    18,    19,    20,
      21,    22,    23,     0,     3,    11,     5,    -1,    29,     8,
      -1,    -1,     3,    12,    13,    14,    15,     8,    -1,    18,
       0,    12,    13,    14,    15,     5,     6,    18,     8,     9,
      10,    11,     4,     5,     6,    -1,     8,     9,    10,    11,
       4,     5,     6,    -1,     8,     9,    10,    11,     4,     5,
       6,    -1,     8,     9,    10,    11,     4,     5,     6,    -1,
       8,     9,    10,    11,     5,     6,    -1,     8,     9,    10,
      11,     5,    -1,    -1,    -1,     9,    10,    11
  };

  const unsigned char
  ExpressionParser::yystos_[] =
  {
       0,     3,     8,    12,    13,    14,    15,    18,    21,    22,
      22,    22,     3,     3,     3,     3,     0,     0,     5,     6,
       8,     9,    10,    11,     4,    22,    22,    22,    22,     5,
      22,    22,    22,    22,    22,    22,     4,     4,     4,     4,
      22
  };

  const unsigned char
  ExpressionParser::yyr1_[] =
  {
       0,    20,    21,    22,    22,    22,    22,    22,    22,    22,
      22,    22,    22,    22,    22,    22,    22
  };

  const unsigned char
  ExpressionParser::yyr2_[] =
  {
       0,     2,     2,     1,     3,     2,     3,     3,     3,     3,
       3,     3,     4,     4,     4,     4,     4
  };



  // YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
  // First, the terminals, then, starting at \a yyntokens_, nonterminals.
  const char*
  const ExpressionParser::yytname_[] =
  {
  "\"end of file\"", "error", "$undefined", "\"(\"", "\")\"", "\"*\"",
  "\"+\"", "\",\"", "\"-\"", "\"/\"", "\"%\"", "\"^\"", "\"COS\"",
  "\"SIN\"", "\"LOG\"", "\"HEX\"", "INVALID", "\"Identifier\"",
  "\"Numeric\"", "UMINUS", "$accept", "document", "expression", YY_NULLPTR
  };

#if YYDEBUG
  const unsigned char
  ExpressionParser::yyrline_[] =
  {
       0,    62,    62,    73,    77,    81,    85,    89,    93,    97,
     101,   105,   109,   113,   117,   121,   125
  };

  // Print the state stack on the debug stream.
  void
  ExpressionParser::yystack_print_ ()
  {
    *yycdebug_ << "Stack now";
    for (stack_type::const_iterator
           i = yystack_.begin (),
           i_end = yystack_.end ();
         i != i_end; ++i)
      *yycdebug_ << ' ' << i->state;
    *yycdebug_ << std::endl;
  }

  // Report on the debug stream that the rule \a yyrule is going to be reduced.
  void
  ExpressionParser::yy_reduce_print_ (int yyrule)
  {
    unsigned int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    // Print the symbols being reduced, and their result.
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
               << " (line " << yylno << "):" << std::endl;
    // The symbols being reduced.
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
                       yystack_[(yynrhs) - (yyi + 1)]);
  }
#endif // YYDEBUG



} // yy
#line 1061 "expression-parser.cc" // lalr1.cc:1155
#line 130 "expression-parser.yy" // lalr1.cc:1156

void yy::ExpressionParser::error(const location_type& l, const std::string& m) {
}
