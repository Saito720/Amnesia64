#include "impl/SqScript.h"
#include "impl/scriptstring.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace hpl;
int hplMain(const hpl::tString&) { return 0; }
static cSqScript* gScript;
static cSqScript* gForeignScript;
static int gSideEffects;
static int gCapturedInt;
static float gCapturedFloat;
static std::string gCapturedString;
static std::string gSnapshotSource;
static unsigned gRecursions;
static bool gInitializationReentryRejected, gInitializationRebuildRejected;
static bool gIgnoredChildRejected;
static CScriptString* StringSnapshot() { return new CScriptString(gSnapshotSource); }
static int SideEffect() { return ++gSideEffects; }
static void CaptureInt(int value) { gCapturedInt=value; }
static void CaptureFloat(float value) { gCapturedFloat=value; }
static void CaptureString(const std::string& value) { gCapturedString=value; }
static void Nested()
{
    tString error;
    if(!gScript->RunTyped(gScript->GetFuncHandleByDecl("void Inner()"), {}, NULL, &error, 1000)) {
        std::cerr << "FAILED nested call: " << error << std::endl;std::exit(1);
    }
}
static void BudgetNested()
{
    tString error;
    // Even an unlimited nested legacy call must honor its budgeted owner.
    if(!gScript->Run("Inner()",&error,0)) asGetActiveContext()->SetException(error.c_str());
}
static void IgnoredBudgetNested()
{
    tString error;
    gIgnoredChildRejected=!gScript->Run("Loop()",&error,0);
}
static void ForeignBudgetNested()
{
    tString error;
    // A client engine can exhaust its calling authority engine's budget.
    gForeignScript->Run("ForeignLoop()",&error,0);
}
static void RecurseNative()
{
    ++gRecursions;
    tString error;
    if(!gScript->RunTyped(gScript->GetFuncHandleByDecl("void Recurse()"),{},NULL,&error,1000))
        asGetActiveContext()->SetException(error.c_str());
}
static int ReenterInitialization()
{
    tString error;
    gInitializationReentryRejected=!gScript->InitializeGlobals(&error,100);
    gInitializationRebuildRejected=!gScript->CreateFromSource("illegal-rebuild.hps","void Rebuilt() {}",&error);
    return 1;
}
static void Check(bool condition, const tString& error="")
{
    if (!condition) { std::cerr << "FAILED: " << error << std::endl; std::exit(1); }
}
int main()
{
    asIScriptEngine* engine=asCreateScriptEngine(ANGELSCRIPT_VERSION);
    Check(engine!=NULL,"create engine");
    cScriptOutput output;
    {
        const tString largeMessage=tString(16000,'x')+"%n%s";
        asSMessageInfo message={"diagnostic.hps",1,1,asMSGTYPE_ERROR,largeMessage.c_str()};
        for(unsigned i=0;i<100;++i) output.AddMessage(&message);
        Check(output.GetMessage().size()<=64*1024,"compiler output has an aggregate limit");
        output.Clear();
        message.message="Literal format directives: %n %s %999999d";
        output.AddMessage(&message);output.Display();output.Clear();
        Error("%s",largeMessage.c_str());Warning("%s",largeMessage.c_str());Log("%s",largeMessage.c_str());
    }
    Check(engine->SetMessageCallback(asMETHOD(cScriptOutput,AddMessage), &output, asCALL_THISCALL)>=0);
    RegisterScriptString(engine);
    Check(engine->RegisterGlobalFunction("int SideEffect()",asFUNCTION(SideEffect),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("void CaptureInt(int)",asFUNCTION(CaptureInt),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("void CaptureFloat(float)",asFUNCTION(CaptureFloat),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("void CaptureString(const string &in)",asFUNCTION(CaptureString),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("string@ StringSnapshot()",asFUNCTION(StringSnapshot),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("void Nested()",asFUNCTION(Nested),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("void BudgetNested()",asFUNCTION(BudgetNested),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("void IgnoredBudgetNested()",asFUNCTION(IgnoredBudgetNested),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("void ForeignBudgetNested()",asFUNCTION(ForeignBudgetNested),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("void RecurseNative()",asFUNCTION(RecurseNative),asCALL_CDECL)>=0);
    Check(engine->RegisterGlobalFunction("int ReenterInitialization()",asFUNCTION(ReenterInitialization),asCALL_CDECL)>=0);
    tString error;
    {
        cSqScript script("typed-test",engine,&output,1);
        gScript=&script;
        Check(script.GetFuncHandle("Missing")<0,"unbuilt lookup");
        Check(!script.InitializeGlobals(&error,100),"unbuilt init rejects");
        Check(script.CreateFromSource("typed-client.hps", R"AS(
            class Initializer { Initializer() { SideEffect(); } }
            Initializer initializer;
            int initialized=1;
            string savedSnapshot="";
            void SaveSnapshot() {
                string@ value=StringSnapshot();
                savedSnapshot=value;
                value+=" edited";
                CaptureString(savedSnapshot+"|"+value);
            }
            void ReadSnapshot() { CaptureString(savedSnapshot+"|"+StringSnapshot()); }
            void Empty() { CaptureInt(initialized); }
            void Text(string value) { CaptureString(value); }
            void RefText(const string &in value) { CaptureString(value); }
            void Step(float value) { CaptureFloat(value); }
            void Ready(int player) { CaptureInt(player); }
            void IntLoop(int player) { while(true) {} }
            void UIntValue(uint player) { CaptureInt(player); }
            void RefInt(const int &in player) { CaptureInt(player); }
            int IntReturn(int player) { return player; }
            void Inner() { CaptureInt(10); }
            void Outer() { Nested(); CaptureInt(20); }
            void ManyNested() { for(int i=0;i<100;i++) BudgetNested(); }
            void IgnoreChildBudget() { IgnoredBudgetNested(); }
            void CrossEngine() { ForeignBudgetNested(); }
            void Recurse() { RecurseNative(); }
            void Fail() { int zero=0; CaptureInt(1/zero); }
            void Loop() { while(true) {} }
            void Overloaded() {}
            void Overloaded(string value) {}
        )AS", &error, false), error);
        Check(gSideEffects==0,"globals deferred");
        Check(script.HasScriptDefinedObjectTypes(),"script object types detected before init");
        Check(engine->GetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD)==1,"property restored");
        Check(!script.RunTyped(script.GetFuncHandleByDecl("void Empty()"),{},NULL,&error),"no execution before init");
        Check(script.HasFunctionNamed("Overloaded")&&!script.HasFunctionNamed("Absent"),"overloads detected by name");
        Check(script.InitializeGlobals(&error,1000),error);
        Check(script.InitializeGlobals(&error,1000)&&gSideEffects==1,"initialize exactly once");
        Check(script.RunTyped(script.GetFuncHandleByDecl("void Empty()"),{},NULL,&error,1000),error);
        Check(gCapturedInt==1,"initialized global visible");
        const std::string literal="quotes\"; injected(); //\nline\\end";
        Check(script.RunTyped(script.GetFuncHandleByDecl("void Text(string)"),{literal},NULL,&error,1000),error);
        Check(gCapturedString==literal,"string passed literally by value");
        Check(script.RunTyped(script.GetFuncHandleByDecl("void RefText(const string &in)"),{literal},NULL,&error,1000),error);
        Check(gCapturedString==literal,"string passed literally by reference");
        gSnapshotSource="original";
        Check(script.RunTyped(script.GetFuncHandleByDecl("void SaveSnapshot()"),{},NULL,&error,1000),error);
        Check(gCapturedString=="original|original edited" && gSnapshotSource=="original","native string result is a mutable script-owned copy");
        gSnapshotSource="updated";
        Check(script.RunTyped(script.GetFuncHandleByDecl("void ReadSnapshot()"),{},NULL,&error,1000),error);
        Check(gCapturedString=="original|updated","saved native string result survives source changes and temporary handle cleanup");
        const float step=0.016f;
        Check(script.RunTyped(script.GetFuncHandleByDecl("void Step(float)"),{},&step,&error,1000),error);
        Check(std::fabs(gCapturedFloat-step)<0.00001f,"float passed as float");
        for(const int player:{0,17,-1,-2147483647}) {
            Check(script.RunTypedInt(script.GetFuncHandleByDecl("void Ready(int)"),player,&error,1000),error);
            Check(gCapturedInt==player,"integer actor ID passed exactly");
        }
        Check(!script.RunTypedInt(script.GetFuncHandleByDecl("void Step(float)"),17,&error),"integer call rejects float parameter");
        Check(!script.RunTypedInt(script.GetFuncHandleByDecl("void UIntValue(uint)"),17,&error),"integer call rejects unsigned parameter");
        Check(!script.RunTypedInt(script.GetFuncHandleByDecl("void RefInt(const int &in)"),17,&error),"integer call rejects reference parameter");
        Check(!script.RunTypedInt(script.GetFuncHandleByDecl("int IntReturn(int)"),17,&error),"integer call requires void return");
        Check(!script.RunTypedInt(script.GetFuncHandleByDecl("void Empty()"),17,&error),"integer call rejects wrong argument count");
        Check(!script.RunTypedInt(script.GetFuncHandleByDecl("void IntLoop(int)"),17,&error,20),"integer hook loop aborts");
        Check(error.find("budget exceeded")!=tString::npos,error);
        Check(!script.RunTyped(script.GetFuncHandleByDecl("void IgnoreChildBudget()"),{},NULL,&error,20) && gIgnoredChildRejected,"ignored nested error still rejects exhausted owner");
        Check(error.find("budget exceeded")!=tString::npos,error);
        Check(!script.Run("Loop()",&error,20),"authority text callback loop aborts");
        Check(error.find("budget exceeded")!=tString::npos && error.find("typed-client.hps")!=tString::npos,error);
        Check(!script.Run("while(true) {}",&error,20),"direct text loop aborts");
        Check(error.find("budget exceeded")!=tString::npos,error);
        Check(!script.RunTyped(script.GetFuncHandleByDecl("void ManyNested()"),{},NULL,&error,20),"nested legacy calls share bounded owner budget");
        Check(error.find("budget exceeded")!=tString::npos,error);
        Check(!script.RunTyped(script.GetFuncHandleByDecl("void Recurse()"),{},NULL,&error,1000),"native script recursion is bounded");
        Check(error.find("nesting limit")!=tString::npos && gRecursions<=64,error);
        Check(script.Run("Empty()",&error,1000),"budgets and nesting restore after failure");
        asIScriptEngine* foreignEngine=asCreateScriptEngine(ANGELSCRIPT_VERSION);
        cScriptOutput foreignOutput;
        Check(foreignEngine->SetMessageCallback(asMETHOD(cScriptOutput,AddMessage),&foreignOutput,asCALL_THISCALL)>=0);
        RegisterScriptString(foreignEngine);
        {
            cSqScript foreign("foreign-client",foreignEngine,&foreignOutput,1);gForeignScript=&foreign;
            Check(foreign.CreateFromSource("foreign-client.hps","void ForeignLoop() { while(true) {} }",&error),error);
            Check(!script.RunTyped(script.GetFuncHandleByDecl("void CrossEngine()"),{},NULL,&error,20),"nested foreign engine shares owner allowance");
            Check(error.find("ForeignLoop")!=tString::npos && error.find("foreign-client.hps")!=tString::npos,error);
            gForeignScript=NULL;
        }
        foreignEngine->Release();
        Check(!script.RunTyped(script.GetFuncHandleByDecl("void Step(float)"),{"wrong"},NULL,&error),"wrong types reject");
        Check(!script.RunTyped(script.GetFuncHandleByDecl("void Step(float)"),{},NULL,&error),"wrong count rejects");
        Check(!script.RunTyped(-1,{},NULL,&error),"missing function rejects");
        Check(script.RunTyped(script.GetFuncHandleByDecl("void Outer()"),{},NULL,&error,1000),error);
        Check(gCapturedInt==20,"outer context resumes after nested typed call");
        Check(!script.RunTyped(script.GetFuncHandleByDecl("void Fail()"),{},NULL,&error,1000),"exception reported");
        Check(error.find("Divide by zero")!=tString::npos && error.find("typed-client.hps")!=tString::npos,error);
        Check(!script.RunTyped(script.GetFuncHandleByDecl("void Loop()"),{},NULL,&error,20),"loop aborts");
        Check(error.find("budget exceeded")!=tString::npos,error);
        Check(script.Run("Empty()"),"legacy text runs");
        Check(!script.Run("NoSuchFunction()"),"legacy compile status");
        Check(!script.Run("Fail()"),"legacy exception status");
        cSqScript other("other",engine,&output,2);
        Check(other.CreateFromSource("other.hps","void Other() {}",&error),error);
        Check(!other.HasScriptDefinedObjectTypes(),"function-only module has no script object types");
        Check(!script.RunTyped(other.GetFuncHandleByDecl("void Other()"),{},NULL,&error),"foreign handle rejects");
        Check(!other.CreateFromSource("invalid.hps","void Invalid( {",&error,false),"invalid source rejected");
        Check(engine->GetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD)==1,"property restored after failure");
        Check(!other.InitializeGlobals(&error,100),"failed build not initializable");
        const tString longName(12000,'x');
        Check(!other.CreateFromSource("long-error.hps","void Bad() { "+longName+"(); }",&error,false),"long compiler diagnostic rejected safely");
        Check(!error.empty() && error.size()<=64*1024,"long compiler output bounded");
        Check(other.CreateFromSource("global-loop.hps","class Bad { Bad() { while(true) {} } } Bad x;",&error,false),error);
        Check(!other.InitializeGlobals(&error,20),"global initializer loop aborts");
        Check(error.find("budget exceeded")!=tString::npos,error);
        Check(other.CreateFromSource("global-exception.hps","class Bad { Bad() { int z=0; CaptureInt(1/z); } } Bad x;",&error,false),error);
        Check(!other.InitializeGlobals(&error,1000),"initializer exception rejected");
        Check(error.find("Divide by zero")!=tString::npos,error);
        Check(!other.CreateFromSource("bounded-eager-loop.hps","class Spin { Spin() { while(true) {} } } Spin x; void Ready() {}",&error,true,20),"budgeted eager initializer loop rejects creation");
        Check(error.find("budget exceeded")!=tString::npos && error.find("bounded-eager-loop.hps")!=tString::npos,error);
        Check(!other.RunTyped(other.GetFuncHandleByDecl("void Ready()"),{},NULL,&error,100),"failed eager initializer cannot execute");
        Check(engine->GetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD)==1,"eager budget restores engine initialization choice");
        gScript=&other;
        Check(other.CreateFromSource("initializer-reentry.hps","class Initializer { Initializer() { ReenterInitialization(); } } Initializer initialized; void Ready() {}",&error,true,1000),error);
        Check(gInitializationReentryRejected && gInitializationRebuildRejected,"eager initializer reentry and rebuild reject without replacing its module");
        Check(other.RunTyped(other.GetFuncHandleByDecl("void Ready()"),{},NULL,&error,1000),"module survives rejected initializer reentry");
        gScript=&script;
        Check(script.RunTyped(script.GetFuncHandleByDecl("void Empty()"),{},NULL,&error,1000),"other module unaffected by failures");
    }
    // The backend overload borrows rather than releases the supplied context.
    {
        asIScriptModule* module=engine->GetModule("borrowed",asGM_ALWAYS_CREATE);
        Check(module->AddScriptSection("borrowed","int x=1; void Ready() {}")>=0);
        Check(module->Build()>=0,"legacy eager build");
        asIScriptContext* context=engine->CreateContext();
        Check(module->ResetGlobalVars(context)>=0,"borrowed initializer context");
        Check(context->Prepare(module->GetFunctionIdByDecl("void Ready()"))>=0,"borrowed context remains valid");
        Check(context->Execute()==asEXECUTION_FINISHED,"borrowed context executes");
        context->Release();
        Check(module->ResetGlobalVars()>=0,"legacy reset remains valid");
        asIScriptEngine* foreign=asCreateScriptEngine(ANGELSCRIPT_VERSION);
        asIScriptContext* foreignContext=foreign->CreateContext();
        Check(module->ResetGlobalVars(foreignContext)==asINVALID_ARG,"foreign initialization context rejects");
        foreignContext->Release();foreign->Release();
    }
    engine->Release();
    std::cout << "PASS: deferred initialization, typed values, nested calls, diagnostics, isolation, execution and initializer budgets" << std::endl;
}
