//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Intermediate Code Context
// 中间代码上下文 —— TJS2 字节码生成器、寄存器分配与 VM 操作码定义
//---------------------------------------------------------------------------
#ifndef tjsInterCodeGenH
#define tjsInterCodeGenH

#include <vector>
#include <stack>
#include <list>
#include "tjsVariant.h"
#include "tjsInterface.h"
#include "tjsNamespace.h"
#include "tjsError.h"
#include "tjsObject.h"
#include "tjs.tab.hpp"

#ifdef _DEBUG
#include "tjsDebug.h"
#endif // _DEBUG

namespace TJS {
    class tTJSScriptBlock;

    int yylex(parser::value_type *yylex, tTJSScriptBlock *ptr);
    int _yyerror(const tjs_char *msg, tTJSScriptBlock *pm, tjs_int pos = -1);

// VM code address <-> integer index conversion (code addr unit = sizeof(uint32))
// VM 代码地址与整数索引互转（代码地址单位 = sizeof(uint32)）
#define TJS_TO_VM_CODE_ADDR(x) ((x) * (tjs_int)sizeof(tjs_uint32))
#define TJS_FROM_VM_CODE_ADDR(x) ((tjs_int)(x) / (tjs_int)sizeof(tjs_uint32))
// VM register address <-> integer index conversion (reg addr unit = sizeof(tTJSVariant))
// VM 寄存器地址与整数索引互转（寄存器地址单位 = sizeof(tTJSVariant)）
#define TJS_TO_VM_REG_ADDR(x) ((x) * (tjs_int)sizeof(tTJSVariant))
#define TJS_FROM_VM_REG_ADDR(x) ((tjs_int)(x) / (tjs_int)sizeof(tTJSVariant))
// Advance a code pointer by a byte offset stored as a VM code address
// 将代码指针按 VM 代码地址（字节偏移）向前推进
#define TJS_ADD_VM_CODE_ADDR(dest, x) ((*(char **)&(dest)) += (x))
// Compute the address of register [x] relative to base pointer
// 计算相对于 base 指针的第 [x] 个寄存器地址
#define TJS_GET_VM_REG_ADDR(base, x)                                           \
    ((tTJSVariant *)((char *)(base) + (tjs_int)(x)))
// Dereference register [x] relative to base pointer
// 解引用 base 指针偏移 [x] 处的寄存器
#define TJS_GET_VM_REG(base, x) (*(TJS_GET_VM_REG_ADDR(base, x)))
// Expand one opcode into its four variants: base, PD (property-direct),
// PI (property-indirect), P (property-generic)
// 将一个操作码展开为四种形式：普通、PD（直接属性）、PI（间接属性）、P（通用属性）
#define TJS_NORMAL_AND_PROPERTY_ACCESSER(x) x, x##PD, x##PI, x##P

    // TJS2 VM opcode enum. Each entry maps 1:1 to a handler in ExecuteCode.
    // TJS2 VM 操作码枚举。每项与 ExecuteCode 中的处理器一一对应。
    enum tTJSVMCodes {

        VM_NOP,     // no operation / 空操作
        VM_CONST,   // load constant: ra[d] = da[s] / 加载常量
        VM_CP,      // copy register: ra[d] = ra[s] / 复制寄存器
        VM_CL,      // clear register: ra[d] = void / 清空寄存器
        VM_CCL,     // clear N consecutive registers / 连续清空 N 个寄存器
        VM_TT,      // test true: flag = (bool)ra[s] / 测试为真
        VM_TF,      // test false: flag = !(bool)ra[s] / 测试为假
        VM_CEQ,     // compare equal (loose): flag = (ra[a] == ra[b]) / 宽松相等比较
        VM_CDEQ,    // compare discern-equal (strict ===): flag = (ra[a] === ra[b]) / 严格相等比较
        VM_CLT,     // compare less-than: flag = (ra[a] < ra[b]) / 小于比较
        VM_CGT,     // compare greater-than: flag = (ra[a] > ra[b]) / 大于比较
        VM_SETF,    // set from flag: ra[d] = flag / 将 flag 写入寄存器
        VM_SETNF,   // set from !flag: ra[d] = !flag / 将 !flag 写入寄存器
        VM_LNOT,    // logical not: ra[d] = !ra[d] / 逻辑非
        VM_NF,      // negate flag: flag = !flag / 翻转 flag
        VM_JF,      // jump if flag / 条件跳转（flag 为真时跳转）
        VM_JNF,     // jump if !flag / 条件跳转（flag 为假时跳转）
        VM_JMP,     // unconditional jump / 无条件跳转

        // Arithmetic/bitwise ops, each in 4 variants (base/PD/PI/P)
        // 算术和位运算，每个有 4 种形式（普通/直接属性/间接属性/通用属性）
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_INC),  // increment / 自增
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_DEC),  // decrement / 自减
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_LOR),  // logical OR assign / 逻辑或赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_LAND), // logical AND assign / 逻辑与赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_BOR),  // bitwise OR assign / 位或赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_BXOR), // bitwise XOR assign / 位异或赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_BAND), // bitwise AND assign / 位与赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_SAR),  // arithmetic right shift assign / 算术右移赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_SAL),  // arithmetic left shift assign / 算术左移赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_SR),   // unsigned right shift assign / 无符号右移赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_ADD),  // add assign / 加法赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_SUB),  // subtract assign / 减法赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_MOD),  // modulo assign / 取模赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_DIV),  // real divide assign / 实数除法赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_IDIV), // integer divide assign / 整数除法赋值
        TJS_NORMAL_AND_PROPERTY_ACCESSER(VM_MUL),  // multiply assign / 乘法赋值

        VM_BNOT,      // bitwise NOT / 按位取反
        VM_TYPEOF,    // typeof operator / typeof 运算符
        VM_TYPEOFD,   // typeof direct member / typeof 直接成员
        VM_TYPEOFI,   // typeof indirect member / typeof 间接成员
        VM_EVAL,      // evaluate expression string / 求值表达式字符串
        VM_EEXP,      // evaluate expression (non-global context) / 非全局上下文求值
        VM_CHKINS,    // instanceof check / instanceof 检查
        VM_ASC,       // get character code / 获取字符码
        VM_CHR,       // create char from code / 由字符码创建字符
        VM_NUM,       // convert to number / 转换为数值
        VM_CHS,       // change sign (negate) / 取反（符号变换）
        VM_INV,       // invalidate object / 使对象失效
        VM_CHKINV,    // check object validity / 检查对象是否有效
        VM_INT,       // convert to integer / 转换为整数
        VM_REAL,      // convert to real / 转换为实数
        VM_STR,       // convert to string / 转换为字符串
        VM_OCTET,     // convert to octet (byte array) / 转换为字节数组
        VM_CALL,      // call function / 调用函数
        VM_CALLD,     // call direct member function / 调用直接成员函数
        VM_CALLI,     // call indirect member function / 调用间接成员函数
        VM_NEW,       // construct object / 构造对象
        VM_GPD,       // get property direct / 直接获取属性
        VM_SPD,       // set property direct / 直接设置属性
        VM_SPDE,      // set property direct (ensure member) / 直接设置属性（确保成员存在）
        VM_SPDEH,     // set property direct (ensure + hidden) / 直接设置属性（确保+隐藏）
        VM_GPI,       // get property indirect / 间接获取属性
        VM_SPI,       // set property indirect / 间接设置属性
        VM_SPIE,      // set property indirect (ensure) / 间接设置属性（确保成员）
        VM_GPDS,      // get property direct (ignore prop handler) / 直接获取属性（忽略 handler）
        VM_SPDS,      // set property direct (ensure+ignore prop) / 直接设置属性（确保+忽略 handler）
        VM_GPIS,      // get property indirect (ignore prop handler) / 间接获取属性（忽略 handler）
        VM_SPIS,      // set property indirect (ensure+ignore prop) / 间接设置属性（确保+忽略 handler）
        VM_SETP,      // set property (generic) / 通用设置属性
        VM_GETP,      // get property (generic) / 通用获取属性
        VM_DELD,      // delete direct member / 删除直接成员
        VM_DELI,      // delete indirect member / 删除间接成员
        VM_SRV,       // set return value / 设置返回值
        VM_RET,       // return from function / 从函数返回
        VM_ENTRY,     // enter try block / 进入 try 块
        VM_EXTRY,     // exit try block / 退出 try 块
        VM_THROW,     // throw exception / 抛出异常
        VM_CHGTHIS,   // change closure 'this' / 修改闭包的 this 对象
        VM_GLOBAL,    // push global object / 压入全局对象
        VM_ADDCI,     // add class instance info / 添加类实例信息
        VM_REGMEMBER, // register object member / 注册对象成员
        VM_DEBUGGER,  // debugger breakpoint / 调试器断点

        __VM_LAST // sentinel, not a real opcode / 哨兵值，不是真实操作码
    };

#undef TJS_NORMAL_AND_PROPERTY_ACCESSER
    //---------------------------------------------------------------------------
    enum tTJSSubType {
        stNone = VM_NOP,
        stEqual = VM_CP,
        stBitAND = VM_BAND,
        stBitOR = VM_BOR,
        stBitXOR = VM_BXOR,
        stSub = VM_SUB,
        stAdd = VM_ADD,
        stMod = VM_MOD,
        stDiv = VM_DIV,
        stIDiv = VM_IDIV,
        stMul = VM_MUL,
        stLogOR = VM_LOR,
        stLogAND = VM_LAND,
        stSAR = VM_SAR,
        stSAL = VM_SAL,
        stSR = VM_SR,

        stPreInc = __VM_LAST,
        stPreDec,
        stPostInc,
        stPostDec,
        stDelete,
        stFuncCall,
        stIgnorePropGet,
        stIgnorePropSet,
        stTypeOf
    };
    //---------------------------------------------------------------------------
    enum tTJSFuncArgType { fatNormal, fatExpand, fatUnnamedExpand };
    //---------------------------------------------------------------------------
    enum tTJSContextType {
        ctTopLevel,
        ctFunction,
        ctExprFunction,
        ctProperty,
        ctPropertySetter,
        ctPropertyGetter,
        ctClass,
        ctSuperClassGetter,
    };

    //---------------------------------------------------------------------------
    //---------------------------------------------------------------------------
    // tTJSExprNode
    //---------------------------------------------------------------------------
    class tTJSExprNode {
        tjs_int Op;
        tjs_int Position;
        std::vector<tTJSExprNode *> *Nodes;
        tTJSVariant *Val;

    public:
        tTJSExprNode();

        ~tTJSExprNode() { Clear(); }

        void Clear();

        void SetOpcode(tjs_int op) { Op = op; }

        void SetPosition(tjs_int pos) { Position = pos; }

        void SetValue(const tTJSVariant &val) {
            if(!Val)
                Val = new tTJSVariant(val);
            else
                Val->CopyRef(val);
        }

        void Add(tTJSExprNode *n);

        tjs_int GetOpcode() const { return Op; }

        tjs_int GetPosition() const { return Position; }

        tTJSVariant &GetValue() {
            if(!Val)
                return *(tTJSVariant *)nullptr;
            return *Val;
        }

        tTJSExprNode *operator[](tjs_int i) const {
            if(!Nodes)
                return nullptr;
            return (*Nodes)[i];
        }

        unsigned int GetSize() const {
            if(Nodes)
                return static_cast<unsigned int>(Nodes->size());
            return 0;
        }

        // for array and dictionary constant value
        void AddArrayElement(const tTJSVariant &val);

        void AddDictionaryElement(const tTJSString &name,
                                  const tTJSVariant &val);
    };
    //---------------------------------------------------------------------------
    // tTJSInterCodeContext - Intermediate Code Context
    //---------------------------------------------------------------------------
    /*
            this class implements iTJSDispatch2;
            the object can be directly treated as function, class,
       property handlers.
    */

    class tTJSScriptBlock;

    class tTJSInterCodeContext : public tTJSCustomObject {
        typedef tTJSCustomObject inherited;

    public:
        tTJSInterCodeContext(tTJSInterCodeContext *parant, const tjs_char *name,
                             tTJSScriptBlock *block, tTJSContextType type);

        ~tTJSInterCodeContext() override;

        // is bytecode export
        static bool IsBytecodeCompile;

    protected:
        void Finalize() override;
        //-------------------------------------------------------
        // compiling stuff

    public:
        void ClearNodesToDelete();

        struct tSubParam {
            tTJSSubType SubType;
            tjs_int SubFlag;
            tjs_int SubAddress;

            tSubParam() { SubType = stNone, SubFlag = 0, SubAddress = 0; }
        };

        struct tSourcePos {
            tjs_int CodePos;
            tjs_int SourcePos;

            static int TJS_USERENTRY SortFunction(const void *a, const void *b);
        };

    private:
        enum tNestType {
            ntBlock,
            ntWhile,
            ntDoWhile,
            ntFor,
            ntSwitch,
            ntIf,
            ntElse,
            ntTry,
            ntCatch,
            ntWith
        };

        struct tNestData {
            tNestType Type;
            tjs_int VariableCount;
            union {
                bool VariableCreated;
                bool IsFirstCase;
            };
            tjs_int RefRegister;
            tjs_int StartIP;
            tjs_int LoopStartIP;
            std::vector<tjs_int> ContinuePatchVector;
            std::vector<tjs_int> ExitPatchVector;
            tjs_int Patch1;
            tjs_int Patch2;
            tTJSExprNode *PostLoopExpr;
        };

        struct tFixData {
            tjs_int StartIP;
            tjs_int Size;
            tjs_int NewSize;
            bool BeforeInsertion;
            tjs_int32 *Code;

            tFixData(tjs_int startip, tjs_int size, tjs_int newsize,
                     tjs_int32 *code, bool beforeinsertion) {
                StartIP = startip, Size = size, NewSize = newsize, Code = code,
                BeforeInsertion = beforeinsertion;
            }

            tFixData(const tFixData &fixdata) {
                Code = nullptr;
                operator=(fixdata);
            }

            tFixData &operator=(const tFixData &fixdata) {
                delete[] Code;
                StartIP = fixdata.StartIP;
                Size = fixdata.Size;
                NewSize = fixdata.NewSize;
                BeforeInsertion = fixdata.BeforeInsertion;
                Code = new tjs_int32[NewSize];
                memcpy(Code, fixdata.Code, sizeof(tjs_int32) * NewSize);
                return *this;
            }

            ~tFixData() { delete[] Code; }
        };

        struct tNonLocalFunctionDecl {
            tjs_int DataPos;
            tjs_int NameDataPos;
            bool ChangeThis;

            tNonLocalFunctionDecl(tjs_int datapos, tjs_int namedatapos,
                                  bool changethis) {
                DataPos = datapos, NameDataPos = namedatapos;
                ChangeThis = changethis;
            }
        };

        struct tFuncArgItem {
            tjs_int Register;
            tTJSFuncArgType Type;

            tFuncArgItem(tjs_int reg, tTJSFuncArgType type = fatNormal) {
                Register = reg;
                Type = type;
            }
        };

        struct tFuncArg {
            bool IsOmit;
            bool HasExpand;
            std::vector<tFuncArgItem> ArgVector;

            tFuncArg() { IsOmit = HasExpand = false; }
        };

        struct tArrayArg {
            tjs_int Object;
            tjs_int Counter;
        };

        // for Bytecode
        struct tProperty {
            const tjs_char *Name;
            const tTJSInterCodeContext *Value;

            tProperty(const tjs_char *name, const tTJSInterCodeContext *val) {
                Name = name;
                Value = val;
            }
        };

        std::vector<tProperty *> *Properties;

        tjs_int FrameBase;

        tjs_int32 *CodeArea;
        tjs_int CodeAreaCapa;
        tjs_int CodeAreaSize;

        tTJSVariant **_DataArea;
        tjs_int _DataAreaSize;
        tjs_int _DataAreaCapa;
        tTJSVariant *DataArea;
        tjs_int DataAreaSize;

        tTJSLocalNamespace Namespace;

        std::vector<tTJSExprNode *> NodeToDeleteVector;

        std::vector<tTJSExprNode *> CurrentNodeVector;

        std::stack<tFuncArg> FuncArgStack;

        std::stack<tArrayArg> ArrayArgStack;

        tjs_int PrevIfExitPatch;
        std::vector<tNestData> NestVector;

        std::list<tjs_int> JumpList;
        std::list<tFixData> FixList;

        std::vector<tNonLocalFunctionDecl> NonLocalFunctionDeclVector;

        tjs_int FunctionRegisterCodePoint;

        tjs_int PrevSourcePos;
        bool SourcePosArraySorted;
        //	std::vector<tSourcePos> SourcePosVector;
        tSourcePos *SourcePosArray;
        tjs_int SourcePosArraySize;
        tjs_int SourcePosArrayCapa;

        tTJSExprNode *SuperClassExpr;

        tjs_int VariableReserveCount;

        bool AsGlobalContextMode;

        tjs_int MaxFrameCount;
        tjs_int MaxVariableCount;

        tjs_int FuncDeclArgCount;
        tjs_int FuncDeclUnnamedArgArrayBase;
        tjs_int FuncDeclCollapseBase;

        std::vector<tjs_int> SuperClassGetterPointer;

        tjs_char *Name;
        tTJSInterCodeContext *Parent;
        tTJSScriptBlock *Block;
        tTJS *CachedTJSEngine;
        tTJSContextType ContextType;
        tTJSInterCodeContext *PropSetter;
        tTJSInterCodeContext *PropGetter;
        tTJSInterCodeContext *SuperClassGetter;

#ifdef _DEBUG
        ScopeKey DebuggerScopeKey; //!< for exec
        tTJSVariant *DebuggerRegisterArea; //!< for exec
#endif // _DEBUG

    public:
        tTJSContextType GetContextType() const { return ContextType; }

        const tjs_char *GetContextTypeName() const;

        ttstr GetShortDescription() const;

        ttstr GetShortDescriptionWithClassName() const;

        tTJSScriptBlock *GetBlock() const { return Block; }
        tTJS *GetTJS() const { return CachedTJSEngine; }
        void ClearBlockPointer();

#ifdef _DEBUG
        ttstr GetClassName() const;
        ttstr GetSelfClassName() const;

        const ScopeKey &GetDebuggerScopeKey() { return DebuggerScopeKey; }
        tTJSVariant *GetDebuggerRegisterArea() { return DebuggerRegisterArea; }
        tTJSVariant *GetDebuggerDataArea() { return DataArea; }
#endif // _DEBUG
    private:
        void OutputWarning(const tjs_char *msg, tjs_int pos = -1);

    private:
        tjs_int PutCode(tjs_int32 num, tjs_int32 pos = -1);

        tjs_int PutData(const tTJSVariant &val);

        void AddJumpList() { JumpList.push_back(CodeAreaSize); }

        void SortSourcePos();

        void FixCode();

        void RegisterFunction();

        tjs_int _GenNodeCode(tjs_int &frame, tTJSExprNode *node,
                             tjs_uint32 restype, tjs_int reqresaddr,
                             const tSubParam &param);

        tjs_int GenNodeCode(tjs_int &frame, tTJSExprNode *node,
                            tjs_uint32 restype, tjs_int reqresaddr,
                            const tSubParam &param);

        // restypes
#define TJS_RT_NEEDED 0x0001 // result needed
#define TJS_RT_CFLAG                                                           \
    0x0002 // condition flag needed
           // result value
#define TJS_GNC_CFLAG (1 << (sizeof(tjs_int) * 8 - 1)) // true logic
#define TJS_GNC_CFLAG_I (TJS_GNC_CFLAG + 1) // inverted logic

        void StartFuncArg();

        void AddFuncArg(tjs_int addr, tTJSFuncArgType type = fatNormal);

        void EndFuncArg();

        void AddOmitArg();

        void DoNestTopExitPatch();

        void DoContinuePatch(tNestData &nestdata);

        void ClearLocalVariable(tjs_int top, tjs_int bottom);

        void ClearFrame(tjs_int &frame, tjs_int base = -1);

        static void _output_func(const tjs_char *msg, const tjs_char *comment,
                                 tjs_int addr, const tjs_int32 *codestart,
                                 tjs_int size, void *data);

        static void _output_func_src(const tjs_char *msg, const tjs_char *name,
                                     tjs_int line, void *data);

    public:
        void Commit();

        tjs_uint GetCodeSize() const { return CodeAreaSize; }

        tjs_uint GetDataSize() const { return DataAreaSize; }

        tjs_int CodePosToSrcPos(tjs_int codepos) const;

        tjs_int FindSrcLineStartCodePos(tjs_int codepos) const;

        ttstr GetPositionDescriptionString(tjs_int codepos) const;

        void AddLocalVariable(const tjs_char *name, tjs_int init = 0);

        void InitLocalVariable(const tjs_char *name, tTJSExprNode *node);

        void InitLocalFunction(const tjs_char *name, tjs_int data);

        void CreateExprCode(tTJSExprNode *node);

        void EnterWhileCode(bool do_while);

        void CreateWhileExprCode(tTJSExprNode *node, bool do_while);

        void ExitWhileCode(bool do_while);

        void EnterIfCode();

        void CreateIfExprCode(tTJSExprNode *node);

        void ExitIfCode();

        void EnterElseCode();

        void ExitElseCode();

        void EnterForCode();

        void CreateForExprCode(tTJSExprNode *node);

        void SetForThirdExprCode(tTJSExprNode *node);

        void ExitForCode();

        void EnterSwitchCode(tTJSExprNode *node);

        void ExitSwitchCode();

        void ProcessCaseCode(tTJSExprNode *node);

        void EnterWithCode(tTJSExprNode *node);

        void ExitWithCode();

        void DoBreak();

        void DoContinue();

        void DoDebugger();

        void ReturnFromFunc(tTJSExprNode *node);

        void EnterTryCode();

        void EnterCatchCode(const tjs_char *name);

        void ExitTryCode();

        void ProcessThrowCode(tTJSExprNode *node);

        void CreateExtendsExprCode(tTJSExprNode *node, bool hold);

        void CreateExtendsExprProxyCode(tTJSExprNode *node);

        void EnterBlock();

        void ExitBlock();

        void GenerateFuncCallArgCode();

        void AddFunctionDeclArg(const tjs_char *varname, tTJSExprNode *init);

        void AddFunctionDeclArgCollapse(const tjs_char *varname);

        void SetPropertyDeclArg(const tjs_char *varname);

        const tjs_char *GetName() const;

        void PushCurrentNode(tTJSExprNode *node);

        tTJSExprNode *GetCurrentNode();

        void PopCurrentNode();

        tTJSExprNode *MakeConstValNode(const tTJSVariant &val);

        tTJSExprNode *MakeNP0(tjs_int opecode);

        tTJSExprNode *MakeNP1(tjs_int opecode, tTJSExprNode *node1);

        tTJSExprNode *MakeNP2(tjs_int opecode, tTJSExprNode *node1,
                              tTJSExprNode *node2);

        tTJSExprNode *MakeNP3(tjs_int opecode, tTJSExprNode *node1,
                              tTJSExprNode *node2, tTJSExprNode *node3);

        //----------------------------------------------------------
        // disassembler
        // implemented in tjsDisassemble.cpp

        static tTJSString GetValueComment(const tTJSVariant &val);

        void Disassemble(
            void (*output_func)(const tjs_char *msg, const tjs_char *comment,
                                tjs_int addr, const tjs_int32 *codestart,
                                tjs_int size, void *data),
            void (*output_func_src)(const tjs_char *msg, const tjs_char *name,
                                    tjs_int line, void *data),
            void *data, tjs_int start = 0, tjs_int end = 0);

        void Disassemble(
            std::function<void(const tjs_char *msg, void *data)> output_func,
            void *data, tjs_int start = 0, tjs_int end = 0);

        void Disassemble(tjs_int start = 0, tjs_int end = 0);

        void DisassembleSrcLine(tjs_int codepos);

        //---------------------------------------------------------
        // execute stuff
        // implemented in InterCodeExec.cpp
    private:
        void DisplayExceptionGeneratedCode(tjs_int codepos,
                                           const tTJSVariant *ra);

        void ThrowScriptException(tTJSVariant &val, tTJSScriptBlock *block,
                                  tjs_int srcpos);

        //	void ExecuteAsGlobal(tTJSVariant *result);
        void ExecuteAsFunction(iTJSDispatch2 *objthis, tTJSVariant **args,
                               tjs_int numargs, tTJSVariant *result,
                               tjs_int start_ip);

        tjs_int ExecuteCode(tTJSVariant *ra, tjs_int startip,
                            tTJSVariant **args, tjs_int numargs,
                            tTJSVariant *result, bool tryCatch = false);

        tjs_int ExecuteCodeInTryBlock(tTJSVariant *ra, tjs_int startip,
                                      tTJSVariant **args, tjs_int numargs,
                                      tTJSVariant *result, tjs_int catchip,
                                      tjs_int exobjreg);

        static void ContinuousClear(tTJSVariant *ra, const tjs_int32 *code);

        void GetPropertyDirect(tTJSVariant *ra, const tjs_int32 *code,
                               tjs_uint32 flags) const;

        void SetPropertyDirect(tTJSVariant *ra, const tjs_int32 *code,
                               tjs_uint32 flags) const;

        static void GetProperty(tTJSVariant *ra, const tjs_int32 *code);

        static void SetProperty(tTJSVariant *ra, const tjs_int32 *code);

        static void GetPropertyIndirect(tTJSVariant *ra, const tjs_int32 *code,
                                        tjs_uint32 flags);

        static void SetPropertyIndirect(tTJSVariant *ra, const tjs_int32 *code,
                                        tjs_uint32 flags);

        void OperatePropertyDirect(tTJSVariant *ra, const tjs_int32 *code,
                                   tjs_uint32 ope) const;

        static void OperatePropertyIndirect(tTJSVariant *ra,
                                            const tjs_int32 *code,
                                            tjs_uint32 ope);

        static void OperateProperty(tTJSVariant *ra, const tjs_int32 *code,
                                    tjs_uint32 ope);

        void OperatePropertyDirect0(tTJSVariant *ra, const tjs_int32 *code,
                                    tjs_uint32 ope) const;

        static void OperatePropertyIndirect0(tTJSVariant *ra,
                                             const tjs_int32 *code,
                                             tjs_uint32 ope);

        static void OperateProperty0(tTJSVariant *ra, const tjs_int32 *code,
                                     tjs_uint32 ope);

        void DeleteMemberDirect(tTJSVariant *ra, const tjs_int32 *code);

        static void DeleteMemberIndirect(tTJSVariant *ra,
                                         const tjs_int32 *code);

        void TypeOfMemberDirect(tTJSVariant *ra, const tjs_int32 *code,
                                tjs_uint32 flags) const;

        static void TypeOfMemberIndirect(tTJSVariant *ra, const tjs_int32 *code,
                                         tjs_uint32 flags);

        tjs_int CallFunction(tTJSVariant *ra, const tjs_int32 *code,
                             tTJSVariant **args, tjs_int numargs);

        tjs_int CallFunctionDirect(tTJSVariant *ra, const tjs_int32 *code,
                                   tTJSVariant **args, tjs_int numargs);

        tjs_int CallFunctionIndirect(tTJSVariant *ra, const tjs_int32 *code,
                                     tTJSVariant **args, tjs_int numargs);

        static void AddClassInstanceInfo(tTJSVariant *ra,
                                         const tjs_int32 *code);

        void ProcessStringFunction(const tjs_char *member, const ttstr &target,
                                   tTJSVariant **args, tjs_int numargs,
                                   tTJSVariant *result);

        void ProcessOctetFunction(const tjs_char *member,
                                  const tTJSVariantOctet *target,
                                  tTJSVariant **args, tjs_int numargs,
                                  tTJSVariant *result);

        static void TypeOf(tTJSVariant &val);

        void Eval(tTJSVariant &val, iTJSDispatch2 *objthis, bool resneed);

        static void CharacterCodeOf(tTJSVariant &val);

        static void CharacterCodeFrom(tTJSVariant &val);

        static void InstanceOf(const tTJSVariant &name, tTJSVariant &targ);

        void RegisterObjectMember(iTJSDispatch2 *dest);

        // for Byte code
        static void Add4ByteToVector(std::vector<tjs_uint8> *array, int value) {
            array->push_back((tjs_uint8)((value >> 0) & 0xff));
            array->push_back((tjs_uint8)((value >> 8) & 0xff));
            array->push_back((tjs_uint8)((value >> 16) & 0xff));
            array->push_back((tjs_uint8)((value >> 24) & 0xff));
        }

        static void Add2ByteToVector(std::vector<tjs_uint8> *array,
                                     tjs_int16 value) {
            array->push_back((tjs_uint8)((value >> 0) & 0xff));
            array->push_back((tjs_uint8)((value >> 8) & 0xff));
        }

    public:
        // iTJSDispatch2 implementation
        tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, tTJSVariant *result,
                           tjs_int numparams, tTJSVariant **param,
                           iTJSDispatch2 *objthis) override;

        tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *hint, tTJSVariant *result,
                          iTJSDispatch2 *objthis) override;

        tjs_error PropSet(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *hint, const tTJSVariant *param,
                          iTJSDispatch2 *objthis) override;

        tjs_error CreateNew(tjs_uint32 flag, const tjs_char *membername,
                            tjs_uint32 *hint, iTJSDispatch2 **result,
                            tjs_int numparams, tTJSVariant **param,
                            iTJSDispatch2 *objthis) override;

        tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char *membername,
                               tjs_uint32 *hint, const tjs_char *classname,
                               iTJSDispatch2 *objthis) override;

        tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                           tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

        tjs_error PropSetByVS(tjs_uint32 flag, tTJSVariantString *mambername,
                              const tTJSVariant *param,
                              iTJSDispatch2 *objthis) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error DeleteMember(tjs_uint32 flag, const tjs_char *membername,
                               tjs_uint32 *hint,
                               iTJSDispatch2 *objthis) override;

        tjs_error Invalidate(tjs_uint32 flag, const tjs_char *membername,
                             tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

        tjs_error IsValid(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *hint, iTJSDispatch2 *objthis) override;

        tjs_error Operation(tjs_uint32 flag, const tjs_char *membername,
                            tjs_uint32 *hint, tTJSVariant *result,
                            const tTJSVariant *param,
                            iTJSDispatch2 *objthis) override;

        // for Byte code
        void SetCodeObject(tTJSInterCodeContext *parent,
                           tTJSInterCodeContext *setter,
                           tTJSInterCodeContext *getter,
                           tTJSInterCodeContext *superclass) {
            Parent = parent;
            PropSetter = setter;
            if(setter)
                setter->AddRef();
            PropGetter = getter;
            if(getter)
                getter->AddRef();
            SuperClassGetter = superclass;
#ifdef _DEBUG
            if(Parent)
                Parent->AddRef();
#endif // _DEBUG
        }

        tTJSInterCodeContext(tTJSScriptBlock *block, const tjs_char *name,
                             tTJSContextType type, tjs_int32 *code,
                             tjs_int codeSize, tTJSVariant *data,
                             tjs_int dataSize, tjs_int varcount,
                             tjs_int verrescount, tjs_int maxframe,
                             tjs_int argcount, tjs_int arraybase,
                             tjs_int colbase, bool srcsorted,
                             tSourcePos *srcPos, tjs_int srcPosSize,
                             std::vector<tjs_int> &superpointer);

        std::vector<tjs_uint8> *
        ExportByteCode(bool outputdebug, tTJSScriptBlock *block,
                       class tjsConstArrayData &constarray) const;

    protected:
        void TJSVariantArrayStackAddRef();

        void TJSVariantArrayStackRelease();

        tTJSVariantArrayStack *TJSVariantArrayStack = nullptr;
    };
    //---------------------------------------------------------------------------
} // namespace TJS
#endif
