/* A Bison parser, made by GNU Bison 2.5.  */

/* Bison interface for Yacc-like parsers in C
   
      Copyright (C) 1984, 1989-1990, 2000-2011 Free Software Foundation, Inc.
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.
   
   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     CONST_TOK = 258,
     BOOL_TOK = 259,
     FLOAT_TOK = 260,
     INT_TOK = 261,
     UINT_TOK = 262,
     BREAK = 263,
     CONTINUE = 264,
     DO = 265,
     ELSE = 266,
     FOR = 267,
     IF = 268,
     DISCARD = 269,
     RETURN = 270,
     SWITCH = 271,
     CASE = 272,
     DEFAULT = 273,
     BVEC2 = 274,
     BVEC3 = 275,
     BVEC4 = 276,
     IVEC2 = 277,
     IVEC3 = 278,
     IVEC4 = 279,
     UVEC2 = 280,
     UVEC3 = 281,
     UVEC4 = 282,
     VEC2 = 283,
     VEC3 = 284,
     VEC4 = 285,
     HALF_TOK = 286,
     HVEC2 = 287,
     HVEC3 = 288,
     HVEC4 = 289,
     IN_TOK = 290,
     OUT_TOK = 291,
     INOUT_TOK = 292,
     UNIFORM = 293,
     VARYING = 294,
     GLOBALLYCOHERENT = 295,
     SHARED = 296,
     PRECISE = 297,
     CENTROID = 298,
     NOPERSPECTIVE = 299,
     NOINTERPOLATION = 300,
     LINEAR = 301,
     MAT2X2 = 302,
     MAT2X3 = 303,
     MAT2X4 = 304,
     MAT3X2 = 305,
     MAT3X3 = 306,
     MAT3X4 = 307,
     MAT4X2 = 308,
     MAT4X3 = 309,
     MAT4X4 = 310,
     HMAT2X2 = 311,
     HMAT2X3 = 312,
     HMAT2X4 = 313,
     HMAT3X2 = 314,
     HMAT3X3 = 315,
     HMAT3X4 = 316,
     HMAT4X2 = 317,
     HMAT4X3 = 318,
     HMAT4X4 = 319,
     FMAT2X2 = 320,
     FMAT2X3 = 321,
     FMAT2X4 = 322,
     FMAT3X2 = 323,
     FMAT3X3 = 324,
     FMAT3X4 = 325,
     FMAT4X2 = 326,
     FMAT4X3 = 327,
     FMAT4X4 = 328,
     SAMPLERSTATE = 329,
     SAMPLERSTATE_CMP = 330,
     TEXTURE1D = 331,
     TEXTURE1D_ARRAY = 332,
     TEXTURE2D = 333,
     TEXTURE2D_ARRAY = 334,
     TEXTURE2DMS = 335,
     TEXTURE_EXTERNAL = 336,
     BUFFER = 337,
     STRUCTUREDBUFFER = 338,
     BYTEADDRESSBUFFER = 339,
     TEXTURE2DMS_ARRAY = 340,
     TEXTURE3D = 341,
     TEXTURECUBE = 342,
     TEXTURECUBE_ARRAY = 343,
     RWBUFFER = 344,
     RWTEXTURE1D = 345,
     RWTEXTURE1D_ARRAY = 346,
     RWTEXTURE2D = 347,
     RWTEXTURE2D_ARRAY = 348,
     RWTEXTURE3D = 349,
     RWSTRUCTUREDBUFFER = 350,
     RWBYTEADDRESSBUFFER = 351,
     POINT_TOK = 352,
     LINE_TOK = 353,
     TRIANGLE_TOK = 354,
     LINEADJ_TOK = 355,
     TRIANGLEADJ_TOK = 356,
     POINTSTREAM = 357,
     LINESTREAM = 358,
     TRIANGLESTREAM = 359,
     INPUTPATCH = 360,
     OUTPUTPATCH = 361,
     STRUCT = 362,
     VOID_TOK = 363,
     WHILE = 364,
     CBUFFER = 365,
     REGISTER = 366,
     PACKOFFSET = 367,
     IDENTIFIER = 368,
     TYPE_IDENTIFIER = 369,
     NEW_IDENTIFIER = 370,
     FLOATCONSTANT = 371,
     INTCONSTANT = 372,
     UINTCONSTANT = 373,
     BOOLCONSTANT = 374,
     STRINGCONSTANT = 375,
     FIELD_SELECTION = 376,
     LEFT_OP = 377,
     RIGHT_OP = 378,
     INC_OP = 379,
     DEC_OP = 380,
     LE_OP = 381,
     GE_OP = 382,
     EQ_OP = 383,
     NE_OP = 384,
     AND_OP = 385,
     OR_OP = 386,
     MUL_ASSIGN = 387,
     DIV_ASSIGN = 388,
     ADD_ASSIGN = 389,
     MOD_ASSIGN = 390,
     LEFT_ASSIGN = 391,
     RIGHT_ASSIGN = 392,
     AND_ASSIGN = 393,
     XOR_ASSIGN = 394,
     OR_ASSIGN = 395,
     SUB_ASSIGN = 396,
     INVARIANT = 397,
     LOWER_THAN_ELSE = 398,
     VERSION_TOK = 399,
     EXTENSION = 400,
     LINE = 401,
     COLON = 402,
     EOL = 403,
     INTERFACE = 404,
     OUTPUT = 405,
     PRAGMA_DEBUG_ON = 406,
     PRAGMA_DEBUG_OFF = 407,
     PRAGMA_OPTIMIZE_ON = 408,
     PRAGMA_OPTIMIZE_OFF = 409,
     PRAGMA_INVARIANT_ALL = 410,
     ASM = 411,
     CLASS = 412,
     UNION = 413,
     ENUM = 414,
     TYPEDEF = 415,
     TEMPLATE = 416,
     THIS = 417,
     PACKED_TOK = 418,
     GOTO = 419,
     INLINE_TOK = 420,
     NOINLINE = 421,
     VOLATILE = 422,
     PUBLIC_TOK = 423,
     STATIC = 424,
     EXTERN = 425,
     EXTERNAL = 426,
     LONG_TOK = 427,
     SHORT_TOK = 428,
     DOUBLE_TOK = 429,
     HALF = 430,
     FIXED_TOK = 431,
     UNSIGNED = 432,
     DVEC2 = 433,
     DVEC3 = 434,
     DVEC4 = 435,
     FVEC2 = 436,
     FVEC3 = 437,
     FVEC4 = 438,
     SAMPLER2DRECT = 439,
     SAMPLER3DRECT = 440,
     SAMPLER2DRECTSHADOW = 441,
     SIZEOF = 442,
     CAST = 443,
     NAMESPACE = 444,
     USING = 445,
     ERROR_TOK = 446,
     COMMON = 447,
     PARTITION = 448,
     ACTIVE = 449,
     SAMPLERBUFFER = 450,
     FILTER = 451,
     IMAGE1D = 452,
     IMAGE2D = 453,
     IMAGE3D = 454,
     IMAGECUBE = 455,
     IMAGE1DARRAY = 456,
     IMAGE2DARRAY = 457,
     IIMAGE1D = 458,
     IIMAGE2D = 459,
     IIMAGE3D = 460,
     IIMAGECUBE = 461,
     IIMAGE1DARRAY = 462,
     IIMAGE2DARRAY = 463,
     UIMAGE1D = 464,
     UIMAGE2D = 465,
     UIMAGE3D = 466,
     UIMAGECUBE = 467,
     UIMAGE1DARRAY = 468,
     UIMAGE2DARRAY = 469,
     IMAGE1DSHADOW = 470,
     IMAGE2DSHADOW = 471,
     IMAGEBUFFER = 472,
     IIMAGEBUFFER = 473,
     UIMAGEBUFFER = 474,
     IMAGE1DARRAYSHADOW = 475,
     IMAGE2DARRAYSHADOW = 476,
     ROW_MAJOR = 477,
     COLUMN_MAJOR = 478
   };
#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{

/* Line 2068 of yacc.c  */
#line 64 "../../../../../Source/ThirdParty/hlslcc/hlslcc/src/hlslcc_lib/hlsl_parser.yy"

   int n;
   float real;
   const char *identifier;
   const char *string_literal;

   struct ast_type_qualifier type_qualifier;

   ast_node *node;
   ast_type_specifier *type_specifier;
   ast_fully_specified_type *fully_specified_type;
   ast_function *function;
   ast_parameter_declarator *parameter_declarator;
   ast_function_definition *function_definition;
   ast_compound_statement *compound_statement;
   ast_expression *expression;
   ast_declarator_list *declarator_list;
   ast_struct_specifier *struct_specifier;
   ast_declaration *declaration;
   ast_switch_body *switch_body;
   ast_case_label *case_label;
   ast_case_label_list *case_label_list;
   ast_case_statement *case_statement;
   ast_case_statement_list *case_statement_list;

   struct {
	  ast_node *cond;
	  ast_expression *rest;
   } for_rest_statement;

   struct {
	  ast_node *then_statement;
	  ast_node *else_statement;
   } selection_rest_statement;

   ast_attribute* attribute;
   ast_attribute_arg* attribute_arg;



/* Line 2068 of yacc.c  */
#line 314 "../../../../../Source/ThirdParty/hlslcc/hlslcc/src/hlslcc_lib/hlsl_parser.h"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif



#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif



