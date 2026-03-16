//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Conditional Compile Control
// 条件编译控制 —— 解析 TJS2 预处理器表达式（@if / @ifdef / @ifndef 等），
// 由 TJSPP 命名空间下的 Bison 解析器（tjspp.tab.hpp）驱动
//---------------------------------------------------------------------------

#ifndef tjsCompileControlH
#define tjsCompileControlH

#include "tjsString.h"
#include "tjspp.tab.hpp"

using namespace TJS;

namespace TJSPP {
    //---------------------------------------------------------------------------

    // Evaluates a TJS2 preprocessor expression string and returns its integer result.
    // 计算 TJS2 预处理器表达式字符串，返回整型结果（非零为真）。
    class tTJSPPExprParser {
    public:
        tTJSPPExprParser(tTJS *tjs, const tjs_char *script);

        ~tTJSPPExprParser();

        // Run the parser and return the expression value.
        // 运行解析器，返回表达式求值结果。
        tjs_int32 Parse();

        tTJS *TJS; // owning TJS engine instance / 所属 TJS 引擎实例

        // Called by the lexer to get the next token and its integer value.
        // 由词法器调用，获取下一个 token 及其整型值。
        tjs_int GetNext(tjs_int &value);

        tTJS *GetTJS() { return TJS; }

        // Retrieve a string literal by index (stored during lexing).
        // 按索引取出词法阶段存储的字符串字面量。
        [[nodiscard]] const tjs_char *GetString(tjs_int idx) const;

        tjs_int32 Result{}; // final expression result / 最终表达式结果

    private:
        std::vector<ttstr> IDs; // string literals encountered during lexing / 词法阶段遇到的字符串字面量

        const tjs_char *Script;  // full source text / 完整源文本
        const tjs_char *Current{}; // current parse position / 当前解析位置
    };

    // Bison-generated lexer entry point for the preprocessor parser.
    // 预处理器解析器的 Bison 词法器入口。
    int yylex(parser::value_type *yylex, tTJSPPExprParser *ptr);
} // namespace TJSPP

#endif
