#include "tts_proto_fep.h"

#include <ImplementationProxy.h> // TImplementationProxy
#include <coemain.h> // CCoeEnv
#include <ecom/ecom.h>
#include <fepbase.h>

#include "logging_window_gc.h"

CCoeFepPlugIn* TtsProtoFepPlugin::NewL() {
  // The Phone application doesn't like the indirectly created AKNFEP. The
  // easiest workaround is just to fail in that process: it'll keep the
  // AKNFEP it already has.
  //
  // Things that I tried that didn't work:
  //  - directly hand out the CCoeFepPlugin from the AKNFEP factory function
  //  - use our own CCoeFep-derived class that delegates to the one from AKNFEP.
  //  - call REComSession::FinalClose() judiciously to make sure the AKNFEP
  //    has been unloaded before we load it again.
  //  - call CancelTransaction() on the AKNFEP CCoeFep in case it has some
  //    lingering state.
  RProcess me;
  me.Open(me.Id());
  if (me.FileName().CompareF(_L("z:\\sys\\bin\\phone.exe")) == 0 ) {
    me.Close();
    User::Leave(KErrAlreadyExists);
  }
  me.Close();

  TtsProtoFepPlugin* self = new (ELeave) TtsProtoFepPlugin;
  CleanupStack::PushL(self);
  self->ConstructL();
  CleanupStack::Pop(self);
  return self;
}

TtsProtoFepPlugin::~TtsProtoFepPlugin() {
#ifdef __WINS__
  // Deleting the AKNFEP plugin here makes the phone unhappy. Leak it instead.
  delete akn_plugin_;
#endif
  CMyWindowGc::TeardownTls();
  if (original_gc_) {
    Mem::Copy(original_gc_, original_gc_vtbl_, 4);
  }
}

CCoeFep* TtsProtoFepPlugin::NewFepL(CCoeEnv& aCoeEnv,
                                    const CCoeFepParameters& aFepParameters) {
  const TUid aknfepuid = { 0x101fd65a };
  akn_plugin_ = CCoeFepPlugIn::NewL(aknfepuid);
  return akn_plugin_->NewFepL(aCoeEnv, aFepParameters);
}


void TtsProtoFepPlugin::SynchronouslyExecuteSettingsDialogL(
    CCoeEnv& aConeEnvironment) {
  akn_plugin_->SynchronouslyExecuteSettingsDialogL(aConeEnvironment);
}

TtsProtoFepPlugin::TtsProtoFepPlugin() {
}

void TtsProtoFepPlugin::ConstructL() {
  return;
  CMyWindowGc::SetupTls();
  CCoeEnv* env = CCoeEnv::Static();
  CMyWindowGc* myGc = new (ELeave) CMyWindowGc(env->ScreenDevice());
  original_gc_ = env->SwapSystemGc(myGc);
  Mem::Copy(original_gc_vtbl_, original_gc_, 4);
  Mem::Copy(original_gc_, myGc, 4); // replace the vtable
  env->SwapSystemGc(original_gc_);
  delete myGc;
}

// 
const TImplementationProxy ImplementationTable[] = {
        IMPLEMENTATION_PROXY_ENTRY(0x20006E90, TtsProtoFepPlugin::NewL) };

EXPORT_C const TImplementationProxy* ImplementationGroupProxy(TInt& aTableCount) {
  aTableCount = sizeof(ImplementationTable) / sizeof(TImplementationProxy);
  return ImplementationTable;
}
