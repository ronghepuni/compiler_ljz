%option noyywrap
%{
/***************** 声明和选项设置 *****************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syntax_tree.h"
#include "syntax_analyzer.h"

int lines = 1;       // 当前行号
int pos_start = 1;   // token 起始位置
int pos_end = 1;     // token 结束位置

// 传递当前 token 的文本内容给语法分析器
void pass_node(char *text) {
    yylval.node = new_syntax_tree_node(text);
}
%}

%x COMMENT  

%%

 /***************** 关键字 *****************/
"else"      { pos_start = pos_end; pos_end += 4; pass_node(yytext); return ELSE; }
"if"        { pos_start = pos_end; pos_end += 2; pass_node(yytext); return IF; }
"int"       { pos_start = pos_end; pos_end += 3; pass_node(yytext); return INT; }
"float"     { pos_start = pos_end; pos_end += 5; pass_node(yytext); return FLOAT; }
"return"    { pos_start = pos_end; pos_end += 6; pass_node(yytext); return RETURN; }
"void"      { pos_start = pos_end; pos_end += 4; pass_node(yytext); return VOID; }
"while"     { pos_start = pos_end; pos_end += 5; pass_node(yytext); return WHILE; }

 /***************** 专用符号 *****************/
"+"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return ADD; }
"-"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return SUB; }
"*"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return MUL; }
"/"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return DIV; }

"<"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return LT; }
"<="        { pos_start = pos_end; pos_end += 2; pass_node(yytext); return LTE; }
">"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return GT; }
">="        { pos_start = pos_end; pos_end += 2; pass_node(yytext); return GTE; }
"=="        { pos_start = pos_end; pos_end += 2; pass_node(yytext); return EQ; }
"!="        { pos_start = pos_end; pos_end += 2; pass_node(yytext); return NEQ; }

"="         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return ASSIGN; }
";"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return SEMICOLON; }
","         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return COMMA; }

"("         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return LPARENTHESE; }
")"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return RPARENTHESE; }
"["         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return LBRACKET; }
"]"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return RBRACKET; }
"{"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return LBRACE; }
"}"         { pos_start = pos_end; pos_end += 1; pass_node(yytext); return RBRACE; }

 /***************** 标识符和数值 *****************/
[a-zA-Z]+   { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return IDENTIFIER; }  // ID = letter+
[0-9]+      { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return INTEGER; }    // INTEGER = digit+
[0-9]+\.[0-9]*|[0-9]*\.[0-9]+ { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return FLOATPOINT; }  // FLOATPOINT

 /***************** 注释处理 *****************/
"/*"        { pos_start = pos_end; pos_end += 2; BEGIN(COMMENT); }  // 进入注释状态
<COMMENT>"*/" { pos_start = pos_end; pos_end += 2; BEGIN(INITIAL); }  // 退出注释状态
<COMMENT>.  { pos_start = pos_end; pos_end += 1; }  // 注释内字符（忽略）
<COMMENT>\n { lines++; pos_start = 1; pos_end = 1; }  // 注释内换行（行号+1）

 /***************** 空白字符和换行 *****************/
[ \t]+      { pos_start = pos_end; pos_end += strlen(yytext); }  // 忽略空格和制表符
\n          { lines++; pos_start = 1; pos_end = 1; }  // 换行（行号+1，位置重置）

 /***************** 非法字符处理 *****************/
.           { pos_start = pos_end; pos_end += 1; return ERROR; }  // 其他字符报错

%%